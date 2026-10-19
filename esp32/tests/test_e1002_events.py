"""Host checks for retained events and asynchronous command completion.

Does not build or flash ESP32 firmware.
"""
import os
from pathlib import Path
import shlex
import subprocess
import tempfile
import unittest

ROOT = Path(__file__).resolve().parents[1]
JSON = Path(os.environ.get("CJSON_SOURCE_DIR", ROOT / "managed_components/espressif__cjson/cJSON"))


class E1002EventsTest(unittest.TestCase):
    def test_history_wait_timeout_and_session_binding(self):
        with tempfile.TemporaryDirectory() as directory:
            out = Path(directory)
            (out / "freertos").mkdir()
            (out / "freertos/FreeRTOS.h").write_text(
                "#pragma once\n#define pdPASS 1\n#define pdMS_TO_TICKS(x) (x)\n"
                "typedef int portMUX_TYPE;\n#define portMUX_INITIALIZER_UNLOCKED 0\n"
                "#define portENTER_CRITICAL(x) ((void)(x))\n"
                "#define portEXIT_CRITICAL(x) ((void)(x))\n")
            (out / "freertos/task.h").write_text(
                "#pragma once\n#include <stddef.h>\n"
                "typedef void (*TaskFunction_t)(void *);\n"
                "int xTaskCreate(TaskFunction_t,const char *,unsigned,void *,unsigned,void *);\n"
                "void vTaskDelay(int);\nvoid vTaskDelete(void *);\n")
            (out / "esp_timer.h").write_text(
                "#pragma once\n#include <stdint.h>\nint64_t esp_timer_get_time(void);\n")
            exe = out / "events.exe"
            compiler = shlex.split(os.environ.get("CC", "gcc"))
            cxx = shlex.split(os.environ.get("CXX", "g++"))
            noise = (ROOT / "main/noise_control.cpp").read_text(encoding="utf-8")
            start = noise.index("static char *wrap_result_json(")
            end = noise.index("// ---- Reconnect policy", start)
            (out / "noise_result.inc").write_text(noise[start:end], encoding="utf-8")
            flags = ["-Wall", "-Wextra", "-Werror", "-I", str(out),
                     "-I", str(JSON), "-I", str(ROOT / "main")]
            objects = []
            for source in [ROOT / "tests/e1002_events_harness.c",
                           ROOT / "main/e1002_events.c", JSON / "cJSON.c"]:
                obj = out / (source.stem + ".o")
                result = subprocess.run([*compiler, "-std=c11", *flags,
                                         "-c", str(source), "-o", str(obj)],
                                        capture_output=True, text=True)
                self.assertEqual(result.returncode, 0, result.stdout + result.stderr)
                objects.append(str(obj))
            result = subprocess.run([
                *cxx, "-std=c++17", *flags,
                str(ROOT / "tests/e1002_events_wire_harness.cpp"), *objects,
                "-lm", "-o", str(exe)], capture_output=True, text=True)
            self.assertEqual(result.returncode, 0, result.stdout + result.stderr)
            result = subprocess.run([str(exe)], capture_output=True, text=True)
            self.assertEqual(result.returncode, 0, result.stdout + result.stderr)


if __name__ == "__main__":
    unittest.main()
