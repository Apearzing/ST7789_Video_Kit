#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""Stream a video file (or webcam) to STM32 + ST7789 over UART.

Frame packet (little endian):
    0x55 0xAA | W:u16 | H:u16 | LEN:u32 | DATA[LEN] | CHECKSUM:u16
DATA is RGB565, high byte first. CHECKSUM = sum(DATA) & 0xFFFF.
MCU replies 0x06 (ACK, frame shown) or 0x15 (NACK, resend).

Example:
    python video_to_uart.py bad_apple.mp4 --port COM3 --baud 921600 --loop
    python video_to_uart.py CAM:0 --port COM3 --baud 921600 --preview
"""
import argparse
import struct
import sys
import time

import cv2
import numpy as np
import serial

MAGIC = b"\x55\xAA"
ACK = 0x06
NACK = 0x15


def _prep(bgr, w, h, stretch=False):
    """Convert a BGR frame to RGB565 big-endian bytes (w*h*2).

    Default keeps the aspect ratio: scale until the frame COVERS w*h,
    then center-crop (no stretching). stretch=True restores the old
    full-frame stretch behavior.
    """
    if stretch:
        small = cv2.resize(bgr, (w, h), interpolation=cv2.INTER_AREA)
    else:
        vh, vw = bgr.shape[:2]
        s = max(w / vw, h / vh)
        nw, nh = max(1, int(vw * s + 0.5)), max(1, int(vh * s + 0.5))
        rs = cv2.resize(bgr, (nw, nh), interpolation=cv2.INTER_AREA)
        x0, y0 = (nw - w) // 2, (nh - h) // 2
        small = rs[y0:y0 + h, x0:x0 + w]
    return small


def to_rgb565be(bgr, w, h, stretch=False):
    small = _prep(bgr, w, h, stretch)
    rgb = cv2.cvtColor(small, cv2.COLOR_BGR2RGB).astype(np.uint16)
    v = (((rgb[:, :, 0] >> 3) << 11) | (((rgb[:, :, 1] >> 2) << 5) |
         (rgb[:, :, 2] >> 3)))
    return v.astype(">u2").tobytes()


# 4x4 Bayer matrix for ordered dithering (spreads quantization error
# into high-frequency noise so gradients/saturation look natural).
_BAYER4 = np.array([[0, 8, 2, 10], [12, 4, 14, 6],
                    [3, 11, 1, 9], [15, 7, 13, 5]], dtype=np.int16)


def to_rgb332(bgr, w, h, stretch=False):
    small = _prep(bgr, w, h, stretch)
    rgb = cv2.cvtColor(small, cv2.COLOR_BGR2RGB).astype(np.int16)
    h, w = rgb.shape[:2]
    t = np.tile(_BAYER4, ((h + 3) // 4, (w + 3) // 4))[:h, :w]
    r = np.clip(((rgb[:, :, 0] + t * 2 - 15) >> 5), 0, 7)
    g = np.clip(((rgb[:, :, 1] + t * 2 - 15) >> 5), 0, 7)
    b = np.clip(((rgb[:, :, 2] + t * 4 - 30) >> 6), 0, 3)
    v = ((r << 5) | (g << 2) | b).astype(np.uint8)
    v[rgb.max(axis=2) < 20] = 0
    return v.tobytes()


def open_serial(port, baud, tries=3):
    """Open the port with retries; raises the last error if all fail."""
    err = None
    for i in range(tries):
        try:
            return serial.Serial(port, baud, timeout=0.2)
        except Exception as e:  # noqa: BLE001
            err = e
            print(f"打开串口失败 ({i + 1}/{tries}): {e}")
            print("请检查: 串口号是否正确 / 是否被串口助手/Keil占用 / 换个USB口重插")
            time.sleep(1.0)
    raise err


def wait_ack(ser, timeout=3.0):
    """True=ACK, False=NACK, None=timeout."""
    deadline = time.time() + timeout
    while time.time() < deadline:
        b = ser.read(1)
        if not b:
            continue
        if b[0] == ACK:
            return True
        if b[0] == NACK:
            return False
    return None


def main():
    ap = argparse.ArgumentParser(description="Play video on ST7789 via UART")
    ap.add_argument("video", help="video file path, or CAM:N for webcam")
    ap.add_argument("--port", required=True, help="e.g. COM3 or /dev/ttyUSB0")
    ap.add_argument("--baud", type=int, default=1500000)
    ap.add_argument("--size", type=int, default=120, choices=[120, 90, 80, 60],
                    help="sent frame W=H, screen auto-scales to fill "
                         "(ACK mode @1500000 pipelined: 120~5fps, 90~9fps, 80~17fps, 60~17fps)")
    ap.add_argument("--depth", type=int, default=8, choices=[16, 8],
                    help="color depth: 16=RGB565 (clear), 8=RGB332 (half size, ~2x fps)")
    ap.add_argument("--fps", type=float, default=0,
                    help="cap sender fps, 0 = as fast as ACKs allow")
    ap.add_argument("--loop", action="store_true", help="loop the video file")
    ap.add_argument("--preview", action="store_true", help="show preview window")
    ap.add_argument("--stretch", action="store_true",
                    help="stretch frame to fill (default: keep aspect, center-crop)")
    ap.add_argument("--no-ack", action="store_true",
                    help="one-way mode: send without waiting ACK "
                         "(use when only TX+GND are wired, no RX line)")
    args = ap.parse_args()

    use_ack = not args.no_ack
    auto_pace = args.no_ack and args.fps <= 0
    if auto_pace:
        print("单向模式: 按串口线速推送，无额外等待 (可用 --fps 覆盖)")
    fails = 0

    if args.video.upper().startswith("CAM:"):
        cap = cv2.VideoCapture(int(args.video[4:]))
    else:
        cap = cv2.VideoCapture(args.video)
    if not cap.isOpened():
        sys.exit("cannot open video source: " + args.video)

    ser = open_serial(args.port, args.baud)
    time.sleep(1.5)  # let the USB-UART adapter settle
    ser.reset_input_buffer()
    print(f"opened {args.port} @ {args.baud}, sending {args.size}x{args.size} ...")
    print("TIP: press RESET on the board, video starts after the direction card.")

    min_interval = 1.0 / args.fps if args.fps > 0 else 0
    n = 0
    t0 = time.time()
    try:
        while True:
            ok, frame = cap.read()
            if not ok:
                if args.loop:
                    cap.set(cv2.CAP_PROP_POS_FRAMES, 0)
                    continue
                break
            if args.depth == 8:
                data = to_rgb332(frame, args.size, args.size, stretch=args.stretch)
            else:
                data = to_rgb565be(frame, args.size, args.size, stretch=args.stretch)
            pkt = (MAGIC + struct.pack("<HHI", args.size, args.size, len(data))
                   + data + struct.pack("<H", sum(data) & 0xFFFF))
            t_send = time.time()
            if use_ack:
                for _ in range(4):
                    ser.write(pkt)
                    r = wait_ack(ser)
                    if r is True:
                        fails = 0
                        break
                    if r is None:
                        fails += 1
                        print("ACK timeout, resend...")
                        if fails >= 3:
                            use_ack = False
                            auto_pace = True
                            print("连续3帧无ACK，自动切换单向模式（请检查RXD-PA9回线）")
                            break
                    else:
                        fails = 0
                        print("NACK, resend...")
                    ser.reset_input_buffer()
                else:
                    if not auto_pace:
                        print("frame dropped (no ACK)")
            else:
                ser.write(pkt)
            if args.preview:
                cv2.imshow("screen-preview", cv2.resize(frame, (240, 240)))
                if cv2.waitKey(1) == 27:
                    break
            n += 1
            if min_interval:
                dt = time.time() - t_send
                if dt < min_interval:
                    time.sleep(min_interval - dt)
            elif auto_pace:
                # pipelined MCU drains ~58ms/frame: never pace below that
                time.sleep(max(len(pkt) * 10.0 / args.baud, 0.06) + 0.005)
            if n % 30 == 0:
                print(f"{n} frames, {n / (time.time() - t0):.1f} fps")
    except KeyboardInterrupt:
        pass
    print("done,", n, "frames")


if __name__ == "__main__":
    main()