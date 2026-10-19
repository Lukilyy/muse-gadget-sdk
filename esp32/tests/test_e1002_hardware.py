"""Host checks only: command ownership, validation and full registration size."""
import os
from pathlib import Path
import shlex
import subprocess
import tempfile
import unittest

ROOT = Path(__file__).resolve().parents[1]
JSON = Path(os.environ.get("CJSON_SOURCE_DIR", ROOT / "managed_components/espressif__cjson/cJSON"))


class E1002HardwareTest(unittest.TestCase):
    def test_commands_and_registration(self):
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
            (out / "esp_log.h").write_text("#pragma once\n#define ESP_LOGW(...) ((void)0)\n")
            noise = (ROOT / "main/noise_control.cpp").read_text(encoding="utf-8")
            start = noise.index("static cJSON *string_param(")
            end = noise.index("// ---- OTA callback", start)
            (out / "noise_register.inc").write_text(noise[start:end], encoding="utf-8")
            start = noise.index("static char *wrap_result_json(")
            end = noise.index("// ---- Reconnect policy", start)
            (out / "noise_result.inc").write_text(noise[start:end], encoding="utf-8")
            flags = ["-Wall", "-Wextra", "-Werror", "-I", str(out),
                     "-I", str(JSON), "-I", str(ROOT / "main")]
            cc = shlex.split(os.environ.get("CC", "gcc"))
            cxx = shlex.split(os.environ.get("CXX", "g++"))
            objects = []
            for source in [ROOT / "tests/e1002_hardware_harness.c",
                           ROOT / "main/e1002_hardware.c", JSON / "cJSON.c"]:
                obj = out / (source.stem + ".o")
                result = subprocess.run([*cc, "-std=c11", *flags, "-c", str(source),
                                         "-o", str(obj)], capture_output=True, text=True)
                self.assertEqual(result.returncode, 0, result.stdout + result.stderr)
                objects.append(str(obj))
            exe = out / "hardware.exe"
            result = subprocess.run([*cxx, "-std=c++17", *flags,
                                     str(ROOT / "tests/e1002_register_harness.cpp"),
                                     *objects, "-lm", "-o", str(exe)], capture_output=True, text=True)
            self.assertEqual(result.returncode, 0, result.stdout + result.stderr)
            result = subprocess.run([str(exe)], capture_output=True, text=True)
            self.assertEqual(result.returncode, 0, result.stdout + result.stderr)


if __name__ == "__main__":
    unittest.main()
