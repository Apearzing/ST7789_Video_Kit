#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""ST7789 video sender - desktop GUI. Streams video to STM32 over UART."""
import struct
import threading
import time
import tkinter as tk
from tkinter import filedialog, messagebox, ttk

import cv2
import serial
from serial.tools import list_ports

from video_to_uart import MAGIC, to_rgb565be, to_rgb332

ACK = 0x06
NACK = 0x15
APP_TITLE = "ST7789 视频推送"


class SenderThread(threading.Thread):
    def __init__(self, cfg, log, on_stat):
        super().__init__(daemon=True)
        self.cfg = cfg
        self.log = log
        self.on_stat = on_stat
        self.stop_flag = threading.Event()

    def wait_ack(self, ser):
        deadline = time.time() + 3.0
        while time.time() < deadline:
            if self.stop_flag.is_set():
                return None
            b = ser.read(1)
            if not b:
                continue
            if b[0] == ACK:
                return True
            if b[0] == NACK:
                return False
        return None

    def run(self):
        cfg = self.cfg
        try:
            if cfg["source"].upper().startswith("CAM:"):
                cap = cv2.VideoCapture(int(cfg["source"][4:]))
            else:
                cap = cv2.VideoCapture(cfg["source"])
            if not cap.isOpened():
                self.log("打不开视频源")
                return
            ser = None
            for i in range(3):
                try:
                    ser = serial.Serial(cfg["port"], cfg["baud"], timeout=0.2)
                    break
                except Exception as e:  # noqa: BLE001
                    self.log(f"打开串口失败({i + 1}/3): {e}")
                    self.log("请检查: 串口号是否正确 / 是否被串口助手占用 / 换个USB口重插")
                    time.sleep(1.0)
            if ser is None:
                return
            time.sleep(1.0)
            ser.reset_input_buffer()
            self.log(f"已打开 {cfg['port']} @ {cfg['baud']}")
            self.log("请按单片机复位键，方向卡后自动播放")
            if not cfg["ack"]:
                self.log("单向模式: 不等ACK, 按5fps推送 (只接TX+GND时用)")
        except Exception as e:  # noqa: BLE001
            self.log(f"打开失败: {e}")
            return

        n = 0
        fails = 0
        t0 = time.time()
        try:
            while not self.stop_flag.is_set():
                ok, frame = cap.read()
                if not ok:
                    if cfg["loop"]:
                        cap.set(cv2.CAP_PROP_POS_FRAMES, 0)
                        continue
                    self.log("视频播完了")
                    break
                if cfg["depth"] == 8:
                    data = to_rgb332(frame, cfg["size"], cfg["size"], stretch=cfg["stretch"])
                else:
                    data = to_rgb565be(frame, cfg["size"], cfg["size"], stretch=cfg["stretch"])
                pkt = (MAGIC + struct.pack("<HHI", cfg["size"], cfg["size"], len(data))
                       + data + struct.pack("<H", sum(data) & 0xFFFF))
                if cfg["ack"]:
                    sent_ok = False
                    for _ in range(4):
                        if self.stop_flag.is_set():
                            break
                        ser.write(pkt)
                        r = self.wait_ack(ser)
                        if r is True:
                            fails = 0
                            sent_ok = True
                            break
                        if r is None:
                            fails += 1
                            self.log("ACK 超时, 重发...")
                            if fails >= 3:
                                cfg["ack"] = False
                                self.log("连续3帧无ACK，已自动切换单向模式（请检查RXD-PA9回线）")
                                self.on_stat("单向模式推送中")
                                break
                        else:
                            fails = 0
                            self.log("NACK, 重发...")
                        ser.reset_input_buffer()
                    if not sent_ok and not self.stop_flag.is_set() and cfg["ack"]:
                        self.log("这一帧丢弃 (无 ACK)")
                else:
                    ser.write(pkt)
                    # one-way mode: wire-speed pacing (one frame time + margin)
                    time.sleep(max(len(pkt) * 10.0 / cfg["baud"], 0.06) + 0.005)
                if cfg["preview"]:
                    cv2.imshow("screen-preview", cv2.resize(frame, (240, 240)))
                    if cv2.waitKey(1) == 27:
                        break
                n += 1
                if n % 10 == 0:
                    fps = n / max(time.time() - t0, 0.01)
                    self.on_stat(f"已发送 {n} 帧, {fps:.1f} fps")
        except Exception as e:  # noqa: BLE001
            self.log(f"发送出错: {e}")
        finally:
            try:
                cap.release()
            except Exception:  # noqa: BLE001
                pass
            try:
                ser.close()
            except Exception:  # noqa: BLE001
                pass
            try:
                cv2.destroyWindow("screen-preview")
            except Exception:  # noqa: BLE001
                pass
            self.log("已停止")
            self.on_stat("待机")


class App(tk.Tk):
    def __init__(self):
        super().__init__()
        self.title(APP_TITLE)
        self.resizable(False, False)
        self.sender = None

        frm = ttk.Frame(self, padding=12)
        frm.grid(row=0, column=0, sticky="nsew")

        # video source
        ttk.Label(frm, text="视频文件:").grid(row=0, column=0, sticky="w")
        self.var_video = tk.StringVar()
        ttk.Entry(frm, textvariable=self.var_video, width=42).grid(row=0, column=1, columnspan=2, sticky="we")
        ttk.Button(frm, text="浏览...", command=self.browse).grid(row=0, column=3, padx=(6, 0))
        ttk.Button(frm, text="摄像头", command=self.use_cam).grid(row=0, column=4, padx=(6, 0))

        # port + baud
        ttk.Label(frm, text="串口:").grid(row=1, column=0, sticky="w", pady=(8, 0))
        self.var_port = tk.StringVar()
        self.cb_port = ttk.Combobox(frm, textvariable=self.var_port, width=18, state="readonly")
        self.cb_port.grid(row=1, column=1, sticky="w", pady=(8, 0))
        ttk.Button(frm, text="刷新", command=self.refresh_ports).grid(row=1, column=2, sticky="w", padx=(6, 0), pady=(8, 0))
        ttk.Label(frm, text="波特率:").grid(row=1, column=3, sticky="e", pady=(8, 0))
        self.var_baud = tk.StringVar(value="1500000")
        ttk.Combobox(frm, textvariable=self.var_baud, width=10, state="readonly",
                     values=["460800", "921600", "1500000", "2000000"]).grid(row=1, column=4, sticky="w", pady=(8, 0))

        # size + options
        ttk.Label(frm, text="分辨率:").grid(row=2, column=0, sticky="w", pady=(8, 0))
        self.var_size = tk.StringVar(value="120")
        ttk.Combobox(frm, textvariable=self.var_size, width=8, state="readonly",
                     values=["120", "90", "80", "60"]).grid(row=2, column=1, sticky="w", pady=(8, 0))
        self.var_loop = tk.BooleanVar(value=True)
        ttk.Checkbutton(frm, text="循环播放", variable=self.var_loop).grid(row=2, column=2, sticky="w", pady=(8, 0))
        self.var_preview = tk.BooleanVar(value=False)
        ttk.Checkbutton(frm, text="预览窗口", variable=self.var_preview).grid(row=2, column=3, sticky="w", pady=(8, 0))
        self.var_ack = tk.BooleanVar(value=True)
        ttk.Checkbutton(frm, text="等待ACK(需接RX线)", variable=self.var_ack).grid(row=3, column=1, sticky="w", pady=(8, 0))
        self.var_stretch = tk.BooleanVar(value=False)
        ttk.Checkbutton(frm, text="拉伸填满(默认按比例截取)", variable=self.var_stretch).grid(row=3, column=2, columnspan=2, sticky="w", pady=(8, 0))
        ttk.Label(frm, text="色彩:").grid(row=4, column=0, sticky="w", pady=(8, 0))
        self.var_depth = tk.StringVar(value="8位（快）")
        ttk.Combobox(frm, textvariable=self.var_depth, width=10, state="readonly",
                     values=["8位（快）", "16位（清）"]).grid(row=4, column=1, sticky="w", pady=(8, 0))

        # buttons
        self.btn_start = ttk.Button(frm, text="开始推送", command=self.start)
        self.btn_start.grid(row=5, column=1, sticky="we", pady=(10, 0))
        self.btn_stop = ttk.Button(frm, text="停止", command=self.stop, state="disabled")
        self.btn_stop.grid(row=5, column=2, sticky="we", pady=(10, 0))

        # status + log
        self.var_stat = tk.StringVar(value="待机")
        ttk.Label(frm, textvariable=self.var_stat, foreground="blue").grid(row=6, column=0, columnspan=5, sticky="w", pady=(8, 0))
        self.txt = tk.Text(frm, width=64, height=10, state="disabled")
        self.txt.grid(row=7, column=0, columnspan=5, pady=(4, 0))

        self.refresh_ports()

    def log(self, msg):
        self.after(0, self._log_in_ui, f"[{time.strftime('%H:%M:%S')}] {msg}")

    def _log_in_ui(self, msg):
        self.txt.configure(state="normal")
        self.txt.insert("end", msg + "\n")
        self.txt.see("end")
        self.txt.configure(state="disabled")

    def on_stat(self, msg):
        self.after(0, self.var_stat.set, msg)

    def browse(self):
        p = filedialog.askopenfilename(title="选择视频",
                                       filetypes=[("视频", "*.mp4 *.avi *.mkv *.mov *.flv *.wmv"), ("全部", "*.*")])
        if p:
            self.var_video.set(p)

    def use_cam(self):
        self.var_video.set("CAM:0")

    def refresh_ports(self):
        ports = [p.device for p in list_ports.comports()]
        self.cb_port["values"] = ports
        if ports and self.var_port.get() not in ports:
            self.var_port.set(ports[0])
        if not ports:
            self.var_port.set("")

    def start(self):
        if self.sender and self.sender.is_alive():
            return
        if not self.var_video.get():
            messagebox.showwarning("提示", "先选择视频文件或摄像头")
            return
        if not self.var_port.get():
            messagebox.showwarning("提示", "没有可用串口，先接上 USB-TTL 并点刷新")
            return
        cfg = {"source": self.var_video.get(), "port": self.var_port.get(),
               "baud": int(self.var_baud.get()), "size": int(self.var_size.get()),
               "loop": self.var_loop.get(), "preview": self.var_preview.get(), "ack": self.var_ack.get(), "stretch": self.var_stretch.get(), "depth": 8 if self.var_depth.get().startswith("8") else 16}
        self.sender = SenderThread(cfg, self.log, self.on_stat)
        self.sender.start()
        self.btn_start.configure(state="disabled")
        self.btn_stop.configure(state="normal")
        self.after(500, self.watch_sender)

    def watch_sender(self):
        if self.sender and self.sender.is_alive():
            self.after(500, self.watch_sender)
        else:
            self.btn_start.configure(state="normal")
            self.btn_stop.configure(state="disabled")

    def stop(self):
        if self.sender:
            self.sender.stop_flag.set()


if __name__ == "__main__":
    App().mainloop()