"""Double-clickable Windows app; Python/Tk and native tools ship in the ZIP."""
import argparse
from datetime import datetime
import json
import os
from pathlib import Path
import queue
import sys
import threading
import traceback
import tkinter as tk
from tkinter import filedialog, messagebox, ttk

from builder import Builder, Cancelled, VERSION, UPSTREAM, CREDIT, copy_instructions


def assets_directory():
    if getattr(sys, "frozen", False):
        return Path(sys.executable).resolve().parent / "assets"
    return Path(__file__).resolve().parents[3] / "build/pc-builder-assets"


class Application:
    def __init__(self):
        if os.name == "nt":
            import ctypes
            ctypes.windll.shell32.SetCurrentProcessExplicitAppUserModelID("Zoria.SwitchWakerHD.Builder.0.0.1")
        self.root = tk.Tk()
        icon = assets_directory() / "resources/branding/builder.ico"
        if icon.is_file():
            self.root.iconbitmap(default=str(icon))
        self.root.title(f"Wind Waker HD - Switch 2 Builder {VERSION}")
        self.root.geometry("860x700")
        self.root.minsize(760, 620)
        self.events = queue.Queue()
        self.worker = None
        self.cancel = threading.Event()
        self.result = None
        self.game = tk.StringVar()
        self.output = tk.StringVar(value=str(Path.home() / "Documents" / "WWHD Switch 2"))
        self.stage = tk.StringVar(value="Choose your Loadiine dump (code, content, meta).")
        self.percent = tk.StringVar(value="0 %")
        body = ttk.Frame(self.root, padding=20)
        body.pack(fill="both", expand=True)
        header = ttk.Frame(body)
        header.pack(fill="x")
        logo = assets_directory() / "resources/branding/logo.png"
        if logo.is_file():
            self.logo = tk.PhotoImage(file=str(logo))
            ttk.Label(header, image=self.logo).pack(side="left", padx=(0, 20))
        ttk.Label(header, text=f"Wind Waker HD\nSwitch 2 Builder - {VERSION}",
                  font=("Segoe UI", 18, "bold")).pack(side="left")
        ttk.Label(body, text="Your own USA/EUR v0 dump is required. Original files are not modified.").pack(anchor="w", pady=(16, 0))
        self.controls = []
        for label, variable, chooser in (("Game dump folder", self.game, self.choose_game),
                                          ("Output folder", self.output, self.choose_output)):
            ttk.Label(body, text=label).pack(anchor="w", pady=(14, 4))
            row = ttk.Frame(body)
            row.pack(fill="x")
            entry = ttk.Entry(row, textvariable=variable)
            entry.pack(side="left", fill="x", expand=True)
            button = ttk.Button(row, text="Browse...", command=chooser)
            button.pack(side="right", padx=(8, 0))
            self.controls += [entry, button]
        buttons = ttk.Frame(body)
        buttons.pack(fill="x", pady=18)
        self.start = ttk.Button(buttons, text="Build NRO and romfs folder", command=self.build)
        self.start.pack(side="left")
        self.cancel_button = ttk.Button(buttons, text="Cancel", command=self.cancel.set, state="disabled")
        self.cancel_button.pack(side="left", padx=10)
        self.open_button = ttk.Button(buttons, text="Open output folder", command=self.open_result, state="disabled")
        self.open_button.pack(side="right")
        ttk.Label(body, textvariable=self.stage, wraplength=790).pack(anchor="w")
        progress = ttk.Frame(body)
        progress.pack(fill="x", pady=(8, 4))
        self.bar = ttk.Progressbar(progress, maximum=100)
        self.bar.pack(side="left", fill="x", expand=True)
        ttk.Label(progress, textvariable=self.percent, width=7, anchor="e").pack(side="right")
        ttk.Label(body, text="Progress is for the current stage, not remaining time. The first build may take a while.",
                  wraplength=790).pack(anchor="w")
        log_frame = ttk.Frame(body)
        log_frame.pack(fill="both", expand=True, pady=12)
        self.messages = tk.Text(log_frame, height=10, wrap="word", state="disabled", font=("Consolas", 9))
        scrollbar = ttk.Scrollbar(log_frame, orient="vertical", command=self.messages.yview)
        self.messages.configure(yscrollcommand=scrollbar.set)
        scrollbar.pack(side="right", fill="y")
        self.messages.pack(side="left", fill="both", expand=True)
        ttk.Label(body, text=f"Original project: {UPSTREAM}\n{CREDIT}", wraplength=790).pack(anchor="w")
        self.root.protocol("WM_DELETE_WINDOW", self.close)
        self.root.after(100, self.pump)

    def choose_game(self):
        value = filedialog.askdirectory(title="Choose the folder containing code, content and meta")
        if value:
            self.game.set(value)

    def choose_output(self):
        value = filedialog.askdirectory(title="Choose where to create the output")
        if value:
            self.output.set(value)

    def message(self, text):
        self.messages.configure(state="normal")
        self.messages.delete("1.0", "end")
        self.messages.insert("end", text)
        self.messages.configure(state="disabled")

    def append_activity(self, text):
        follow = self.messages.yview()[1] >= 0.999
        self.messages.configure(state="normal")
        self.messages.insert("end", f"\n[{datetime.now():%H:%M:%S}] {text}\n")
        # Keep the live viewer bounded. Full compiler diagnostics remain in
        # build.log and recompiler.log in the private build transaction/output.
        if int(self.messages.index("end-1c").split(".")[0]) > 2500:
            self.messages.delete("1.0", "501.0")
        if follow:
            self.messages.see("end")
        self.messages.configure(state="disabled")

    def build(self):
        if not self.game.get().strip() or not self.output.get().strip():
            messagebox.showerror("Folders required", "Choose a game dump and an output folder.")
            return
        game, output = self.game.get(), self.output.get()
        self.cancel = threading.Event()
        self.result = None
        self.last_progress = None
        self.last_log_progress = None
        self.start.configure(state="disabled")
        self.cancel_button.configure(state="normal")
        self.open_button.configure(state="disabled")
        for control in self.controls:
            control.configure(state="disabled")
        self.message("Starting build...\nGame dump: " + game + "\nOutput folder: " + output)
        def progress(stage, done, total):
            state = (stage, min(100, done * 100 // max(1, total)))
            if state != self.last_progress:
                self.last_progress = state
                self.events.put(("progress", state))
        def work():
            builder = Builder(assets_directory(), progress, self.cancel,
                              activity=lambda text: self.events.put(("activity", text)))
            try:
                self.events.put(("success", builder.build(game, output)))
            except BaseException as error:
                details = str(error)
                if builder.work:
                    details += f"\n\nDiagnostics saved in:\n{builder.work}"
                if not isinstance(error, Cancelled):
                    details += "\n\n" + traceback.format_exc()[-2500:]
                self.events.put(("error", details))
        self.worker = threading.Thread(target=work, daemon=False)
        self.worker.start()

    def pump(self):
        try:
            # Process a bounded batch so fast file checks cannot starve Tk's
            # rendering or the Cancel button while the worker queues messages.
            for _ in range(200):
                kind, value = self.events.get_nowait()
                if kind == "progress":
                    self.stage.set(value[0])
                    self.percent.set(f"{value[1]} %")
                    self.bar["value"] = value[1]
                    marker = (value[0], value[1] // 10)
                    if marker != self.last_log_progress:
                        self.last_log_progress = marker
                        self.append_activity(f"{value[0]}: {value[1]}%")
                elif kind == "activity":
                    self.append_activity(value)
                else:
                    self.start.configure(state="normal")
                    self.cancel_button.configure(state="disabled")
                    for control in self.controls:
                        control.configure(state="normal")
                    if kind == "success":
                        self.result = value
                        self.open_button.configure(state="normal")
                        self.stage.set("Build complete. Copy the files to your microSD card.")
                        self.append_activity(copy_instructions() + "\nOutput: " + str(value.directory))
                        messagebox.showinfo("NRO ready to copy", copy_instructions() + "\n" + str(value.directory))
                    else:
                        self.stage.set("Build stopped. Incomplete output is not marked as ready.")
                        self.append_activity("Build stopped:\n" + value)
        except queue.Empty:
            pass
        self.root.after(100, self.pump)

    def open_result(self):
        if self.result:
            os.startfile(self.result.directory)

    def close(self):
        if self.worker and self.worker.is_alive():
            if messagebox.askyesno("Build in progress", "Cancel the build? Wait for it to stop before closing the application."):
                self.cancel.set()
            return
        self.root.destroy()


def main():
    parser = argparse.ArgumentParser(description="Standalone Wind Waker HD Switch 2 Builder")
    parser.add_argument("--assets", type=Path, default=assets_directory())
    parser.add_argument("--self-test", action="store_true")
    parser.add_argument("--build", type=Path, help="Your game dump for a headless build")
    parser.add_argument("--output", type=Path)
    parser.add_argument("--report", type=Path, help="JSON report for automated tests")
    args = parser.parse_args()
    if not args.self_test and not args.build:
        app = Application()
        app.root.mainloop()
        return 0
    result = {}
    status = 0
    try:
        builder = Builder(args.assets)
        if args.self_test:
            result = builder.self_test()
        else:
            if not args.output:
                parser.error("--output is required with --build")
            built = builder.build(args.build, args.output)
            result = {"directory": str(built.directory), "nro": str(built.nro), "romfs": str(built.romfs)}
        result["success"] = True
    except BaseException as error:
        result = {"success": False, "error": str(error), "traceback": traceback.format_exc()}
        if "builder" in locals() and builder.work:
            result["diagnostics"] = str(builder.work)
        status = 1
    if args.report:
        args.report.write_text(json.dumps(result, indent=2, ensure_ascii=False) + "\n", encoding="utf-8")
    elif sys.stdout:
        print(json.dumps(result, indent=2, ensure_ascii=False))
    return status


if __name__ == "__main__":
    raise SystemExit(main())
