# Test builds on the Windows PC

Each passing Windows CI run on `feature/management-ui` publishes the `management-ui-latest` pre-release ([docs/CI.md](../../docs/CI.md)): `retrofe\retrofe.exe`, `layouts\Aeon Nox` and `BUILD.txt`, without the DLLs.

1. `push_to_share.py` (run where the agents work, on Unraid) downloads that zip, checks its SHA-256, and unpacks it on the share as `management-ui\builds\<commit date>-<commit>`. It also copies `Install-TestBuild.ps1` to `management-ui\`, and keeps the five newest builds. It needs `pip install smbprotocol` and a credentials file (`~/.config/coinops/smb.txt`: the user name, then the password).
2. `Install-TestBuild.ps1` (run on the PC) copies the newest build, or `-Build <folder>`, over the install, `E:\Storage\OneSauce` unless `-Install` says otherwise. It stops if that folder has no `retrofe\retrofe.exe` or RetroFE is running from it. Before copying, it saves each file it will replace in `management-ui\backups\<time>`, at the same paths as in the install. It deletes nothing, so artwork kept under `layouts\Aeon Nox\collections` stays.

```
powershell -ExecutionPolicy Bypass -File \\JOHN-DESKTOP\retrofe\management-ui\Install-TestBuild.ps1
```

To go back, copy a backup folder's contents over the install.
