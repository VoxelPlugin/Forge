# Copyright Voxel Plugin SAS. All Rights Reserved.

import json
import os
import sys
import atexit
import requests
import platform
import subprocess

sys.stdout.reconfigure(encoding='utf-8')

GITHUB_URL = os.environ["GITHUB_URL"]
SLACK_BUILD_OPS_URL = os.environ["SLACK_BUILD_OPS_URL"]

if len(sys.argv) < 3:
    print('Invalid usage: should be run.py "Path/To/Project.uproject" MyCommand -Cmd1 -Cmd2')
    exit(1)

project = sys.argv[1]
forge_cmd = sys.argv[2]
forge_args = "" if len(sys.argv) == 3 else " ".join(sys.argv[3:])

print("Project: " + project)
print("Forge Cmd: " + forge_cmd)
print("Forge Args: " + forge_args)
print("OS: " + os.name)
print("Platform: " + platform.processor())

is_unix = os.name == "posix"

if is_unix:
    engine_base_paths = [
        "/Users/Shared",
        "/Users/Shared/Epic Games"]
else:
    engine_base_paths = [
        "C:/BUILD",
        "D:/BUILD",
        "C:/ROOT",
        "D:/ROOT",
        "C:/Program Files/Epic Games"]

engine_path = ""
engine_path_candidates = []
for engine_base_path in engine_base_paths:
    engine_new_path = engine_base_path + "/UE_5.4"
    engine_path_candidates += [engine_new_path]

    if os.path.exists(engine_new_path):
        engine_path = engine_new_path
        break

if not os.path.exists(engine_path):
    print("Failed to find a valid engine install. Checked\n" + "\n".join(engine_path_candidates))
    exit(1)

print("Engine Path: " + engine_path)

print("Killing zombie processes")

if os.name == "posix":
    subprocess.run('killall UnrealEditor', shell=True)
    subprocess.run('killall UnrealEditor-Cmd', shell=True)
    subprocess.run('killall dotnet', shell=True)
else:
    subprocess.run('wmic process where "name=\'UnrealEditor.exe\'" delete')
    subprocess.run('wmic process where "name=\'UnrealEditor-Cmd.exe\'" delete')
    subprocess.run('wmic process where "name=\'dotnet.exe\'" delete')

print("::group::Building project")

if is_unix:
    result = subprocess.run(f'bash "{engine_path}/Engine/Build/BatchFiles/RunUAT.sh" BuildEditor -project="{project}" -notools', shell=True)
else:
    result = subprocess.run(f'"{engine_path}/Engine/Build/BatchFiles/RunUAT.bat" BuildEditor -project="{project}" -notools')

print("::endgroup::")

if result.returncode != 0:
    print("::error::Failed to build project")
    exit(1)

print("Build successful")

saved_directory = os.path.dirname(project) + "/Saved"

if not os.path.exists(saved_directory):
    os.makedirs(saved_directory)

log_path = saved_directory + "/ForgeLog.txt"

with open(log_path, 'w') as f:
    f.write('')

log_file = open(log_path, 'r', encoding='utf-8', errors='backslashreplace')
log_file.read()

print("Starting Unreal")

if is_unix:
    unreal_editor_path = f'{engine_path}/Engine/Binaries/Mac/UnrealEditor.app/Contents/MacOS/UnrealEditor'
else:
    unreal_editor_path = f'{engine_path}/Engine/Binaries/Win64/UnrealEditor.exe'

process = subprocess.Popen(
    f'"{unreal_editor_path}" "{project}" ' +
    f'-unattended -buildmachine -LiveCoding=0 -run=Forge -ForgeCmd={forge_cmd} -ForgeArgs="-DummyArgument {forge_args}"',
    stdout=subprocess.PIPE,
    universal_newlines=True,
    shell=is_unix)

atexit.register(lambda: process.kill())

lines = []
for line in iter(process.stdout.readline, ""):
    line = line.removesuffix("\n")
    lines.append(line)

    log = log_file.read()
    log = log.removesuffix("\n")

    if len(log) > 0:
        print(log)

process.stdout.close()

exit_code = process.wait()

log = log_file.read()
log = log.removesuffix("\n")

if len(log) > 0:
    print(log)

print(f"Exit code: {exit_code}")

# print("::group::Unreal log")
#
# for line in lines:
#     print(line)
#
# print("::endgroup::")

if exit_code != 0:
    print("::error::Command failed")

    payload = {
        "blocks": [
            {
                "type": "section",
                "text": {
                    "type": "mrkdwn",
                    "text": f"*<{GITHUB_URL}|{forge_cmd}>* failed"
                }
            }
        ]
    }

    if "PostFatalSlackMessage" not in "".join(lines):
        response = requests.request(
            "POST",
            SLACK_BUILD_OPS_URL,
            headers={'Content-type': 'application/json'},
            data=json.dumps(payload)).text

        print(response)

    exit(1)

print("Command successful")
