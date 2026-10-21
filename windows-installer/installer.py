import os
import sys
import threading
import subprocess
import tkinter as tk
from tkinter import ttk, messagebox
from pathlib import Path

APP_NAME = "PS4HEN ESP32-S3 Installer"
FLASH_MAP = [
    ("0x0", "bootloader.bin"),
    ("0x8000", "partitions.bin"),
    ("0xE000", "boot_app0.bin"),
    ("0x10000", "firmware.bin"),
    ("0x410000", "spiffs.bin"),
]

def resource_path(*parts):
    root = Path(getattr(sys, "_MEIPASS", Path(__file__).resolve().parent))
    return root.joinpath(*parts)

def run_esptool_mode():
    # PyInstaller bundles esptool into this same executable. Re-enter it as a
    # subprocess so the GUI can display progress without blocking.
    sys.argv = ["esptool"] + sys.argv[2:]
    from esptool.__main__ import main
    main()

if len(sys.argv) > 1 and sys.argv[1] == "--_esptool":
    run_esptool_mode()
    raise SystemExit(0)

try:
    from serial.tools import list_ports
except Exception:
    list_ports = None

class Installer:
    def __init__(self, root):
        self.root = root
        root.title(APP_NAME)
        root.geometry("720x510")
        root.minsize(620, 450)

        outer = ttk.Frame(root, padding=18)
        outer.pack(fill="both", expand=True)

        ttk.Label(outer, text=APP_NAME, font=("Segoe UI", 17, "bold")).pack(anchor="w")
        ttk.Label(
            outer,
            text="Автономная прошивка ESP32-S3 через USB. Интернет и Arduino IDE не нужны.",
            wraplength=660
        ).pack(anchor="w", pady=(5, 16))

        port_row = ttk.Frame(outer)
        port_row.pack(fill="x")
        ttk.Label(port_row, text="USB-порт:").pack(side="left")
        self.port = tk.StringVar()
        self.port_box = ttk.Combobox(port_row, textvariable=self.port, state="readonly", width=22)
        self.port_box.pack(side="left", padx=8)
        ttk.Button(port_row, text="Обновить", command=self.refresh_ports).pack(side="left")
        self.flash_button = ttk.Button(port_row, text="Прошить ESP32-S3", command=self.start_flash)
        self.flash_button.pack(side="right")

        self.status = tk.StringVar(value="Подключите ESP32-S3 по USB и выберите порт.")
        ttk.Label(outer, textvariable=self.status, wraplength=660).pack(anchor="w", pady=(12, 8))

        self.log = tk.Text(outer, height=17, wrap="word", state="disabled", font=("Consolas", 9))
        self.log.pack(fill="both", expand=True)
        ttk.Label(
            outer,
            text="Внимание: устройство должно быть ESP32-S3. Во время прошивки не отключайте USB.",
            wraplength=660
        ).pack(anchor="w", pady=(10, 0))

        self.refresh_ports()
        self.check_firmware()

    def write_log(self, text):
        self.log.configure(state="normal")
        self.log.insert("end", text.rstrip() + "\n")
        self.log.see("end")
        self.log.configure(state="disabled")

    def refresh_ports(self):
        if list_ports is None:
            self.port_box["values"] = []
            self.status.set("Не удалось загрузить модуль обнаружения COM-портов.")
            return
        ports = list(list_ports.comports())
        values = [f"{p.device} — {p.description}" for p in ports]
        self.port_box["values"] = values
        if values:
            self.port_box.current(0)
            self.status.set(f"Найдено портов: {len(values)}. Выберите порт ESP32-S3.")
        else:
            self.port.set("")
            self.status.set("COM-порт не найден. Проверьте USB-кабель и драйвер USB-UART.")

    def check_firmware(self):
        missing = [name for _, name in FLASH_MAP if not resource_path("firmware", name).is_file()]
        if missing:
            self.write_log("ОШИБКА: отсутствуют файлы прошивки: " + ", ".join(missing))
            self.flash_button.configure(state="disabled")
            self.status.set("Комплект прошивки неполный. Скачайте полный комплект установщика.")

    def start_flash(self):
        selected = self.port.get().strip()
        if not selected:
            messagebox.showwarning(APP_NAME, "Сначала выберите USB/COM-порт ESP32-S3.")
            return
        port = selected.split(" — ", 1)[0].strip()
        missing = [name for _, name in FLASH_MAP if not resource_path("firmware", name).is_file()]
        if missing:
            messagebox.showerror(APP_NAME, "Нет файлов: " + ", ".join(missing))
            return
        if not messagebox.askyesno(
            APP_NAME,
            f"Прошить ESP32-S3 через {port}?\n\nТекущая прошивка и данные во flash будут заменены."
        ):
            return
        self.flash_button.configure(state="disabled")
        self.status.set("Идёт прошивка. Не отключайте устройство.")
        self.write_log(f"Начало прошивки: {port}")
        threading.Thread(target=self.flash_worker, args=(port,), daemon=True).start()

    def flash_worker(self, port):
        args = [
            sys.executable, "--_esptool",
            "--chip", "esp32s3",
            "--port", port,
            "--baud", "460800",
            "--before", "default_reset",
            "--after", "hard_reset",
            "write-flash",
            "--flash-size", "16MB",
        ]
        for offset, filename in FLASH_MAP:
            args.extend([offset, str(resource_path("firmware", filename))])

        try:
            result = subprocess.run(args, capture_output=True, text=True, errors="replace", timeout=600)
            output = (result.stdout or "") + ("\n" + result.stderr if result.stderr else "")
            self.root.after(0, self.write_log, output or "(esptool не вернул текстовый вывод)")
            if result.returncode == 0:
                self.root.after(0, self.status.set, "Готово! Прошивка успешно записана.")
                self.root.after(0, self.write_log, "УСПЕХ: прошивка завершена.")
                self.root.after(0, lambda: messagebox.showinfo(APP_NAME, "Прошивка завершена успешно."))
            else:
                self.root.after(0, self.status.set, f"Ошибка прошивки (код {result.returncode}).")
                self.root.after(0, lambda: messagebox.showerror(
                    APP_NAME,
                    "Прошивка не завершена. Проверьте журнал ниже, кабель и драйвер USB."
                ))
        except subprocess.TimeoutExpired:
            self.root.after(0, self.status.set, "Превышено время ожидания прошивки.")
            self.root.after(0, self.write_log, "ОШИБКА: превышено время ожидания (10 минут).")
        except Exception as exc:
            self.root.after(0, self.status.set, "Не удалось запустить прошивку.")
            self.root.after(0, self.write_log, f"ОШИБКА: {exc}")
        finally:
            self.root.after(0, lambda: self.flash_button.configure(state="normal"))

def main():
    root = tk.Tk()
    try:
        ttk.Style().theme_use("vista")
    except tk.TclError:
        pass
    Installer(root)
    root.mainloop()

if __name__ == "__main__":
    main()
