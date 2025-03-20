# Copyright Voxel Plugin SAS. All Rights Reserved.

import json
import os
import sys
import atexit
import requests
import platform
import subprocess

sys.stdout.reconfigure(encoding='utf-8')

if len(sys.argv) < 3:
    print('Invalid usage: should be run.py "Path/To/Project.uproject" MyCommand -Cmd1 -Cmd2', flush=True)
    exit(1)

project = sys.argv[1]
forge_cmd = sys.argv[2]
forge_args = "" if len(sys.argv) == 3 else " ".join(sys.argv[3:])

print("Project: " + project, flush=True)
print("Forge Cmd: " + forge_cmd, flush=True)
print("Forge Args: " + forge_args, flush=True)
print("OS: " + os.name, flush=True)
print("Platform: " + platform.processor(), flush=True)

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
    engine_new_path = engine_base_path + "/UE_5.5"
    engine_path_candidates += [engine_new_path]

    if os.path.exists(engine_new_path):
        engine_path = engine_new_path
        break

if not os.path.exists(engine_path):
    print("Failed to find a valid engine install. Checked\n" + "\n".join(engine_path_candidates), flush=True)
    exit(1)

print("Engine Path: " + engine_path, flush=True)

if "-DoNotKillZombies" not in forge_args:
    print("Killing zombie processes", flush=True)

    if os.name == "posix":
        subprocess.run('killall UnrealEditor', shell=True)
        subprocess.run('killall UnrealEditor-Cmd', shell=True)
        subprocess.run('killall dotnet', shell=True)
    else:
        subprocess.run('wmic process where "name=\'UnrealEditor.exe\'" delete')
        subprocess.run('wmic process where "name=\'UnrealEditor-Cmd.exe\'" delete')
        subprocess.run('wmic process where "name=\'dotnet.exe\'" delete')

print("##teamcity[blockOpened name='Building project' description='Building project']", flush=True)

if is_unix:
    result = subprocess.run(f'bash "{engine_path}/Engine/Build/BatchFiles/RunUAT.sh" BuildEditor -project="{project}" -notools', shell=True)
else:
    result = subprocess.run(f'"{engine_path}/Engine/Build/BatchFiles/RunUAT.bat" BuildEditor -project="{project}" -notools')

print("##teamcity[blockClosed name='Building project']", flush=True)

if result.returncode != 0:
    print("Failed to build project", flush=True)
    exit(1)

print("Build successful", flush=True)

saved_directory = os.path.dirname(project) + "/Saved"

if not os.path.exists(saved_directory):
    os.makedirs(saved_directory)

log_path = saved_directory + "/ForgeLog.txt"

with open(log_path, 'w') as f:
    f.write('')

log_file = open(log_path, 'r', encoding='utf-8', errors='backslashreplace')
log_file.read()

print("Starting Unreal", flush=True)

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
        print(log, flush=True)

process.stdout.close()

exit_code = process.wait()

log = log_file.read()
log = log.removesuffix("\n")

if len(log) > 0:
    print(log, flush=True)

print(f"Exit code: {exit_code}", flush=True)

# print("::group::Unreal log")
#
# for line in lines:
#     print(line)
#
# print("::endgroup::")

if exit_code != 0:
    print("Command failed", flush=True)
    exit(1)

print("Command successful", flush=True)
