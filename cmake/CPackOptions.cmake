# SPDX-License-Identifier: Apache-2.0
# CPack-time configuration for platform-specific installer customization.
#
# This file is included by CPack at pack time (via CPACK_PROJECT_CONFIG_FILE),
# bypassing CPackConfig.cmake serialization. This allows bracket strings with
# embedded quotes and backslashes that would otherwise break the serialized form.

if(CPACK_GENERATOR STREQUAL "NSIS")
    # Add endo's bin directory to the system PATH on install.
    # Uses PowerShell via a temp script to avoid NSIS's ~1024 character string limit,
    # which would corrupt long PATH values.
    set(CPACK_NSIS_EXTRA_INSTALL_COMMANDS [=[
        FileOpen $0 "$TEMP\endo_path_add.ps1" w
        FileWrite $0 "param([string]$$InstallDir)$\r$\n"
        FileWrite $0 "$$p = [Environment]::GetEnvironmentVariable('Path', 'Machine')$\r$\n"
        FileWrite $0 "if ($$p -notlike ('*' + $$InstallDir + '*')) {$\r$\n"
        FileWrite $0 "    [Environment]::SetEnvironmentVariable('Path', $$p + ';' + $$InstallDir, 'Machine')$\r$\n"
        FileWrite $0 "}$\r$\n"
        FileClose $0
        nsExec::ExecToLog 'powershell -NoProfile -ExecutionPolicy Bypass -File "$TEMP\endo_path_add.ps1" -InstallDir "$INSTDIR\bin"'
        Delete "$TEMP\endo_path_add.ps1"
    ]=])

    # Remove endo's bin directory from the system PATH on uninstall.
    set(CPACK_NSIS_EXTRA_UNINSTALL_COMMANDS [=[
        FileOpen $0 "$TEMP\endo_path_remove.ps1" w
        FileWrite $0 "param([string]$$InstallDir)$\r$\n"
        FileWrite $0 "$$p = [Environment]::GetEnvironmentVariable('Path', 'Machine')$\r$\n"
        FileWrite $0 "$$parts = $$p -split ';' | Where-Object { $$_ -ne $$InstallDir }$\r$\n"
        FileWrite $0 "[Environment]::SetEnvironmentVariable('Path', ($$parts -join ';'), 'Machine')$\r$\n"
        FileClose $0
        nsExec::ExecToLog 'powershell -NoProfile -ExecutionPolicy Bypass -File "$TEMP\endo_path_remove.ps1" -InstallDir "$INSTDIR\bin"'
        Delete "$TEMP\endo_path_remove.ps1"
    ]=])
endif()
