# Test builds on the Windows PC

Each passing Windows CI run on `feature/management-ui` publishes the `management-ui-latest` pre-release ([docs/CI.md](../../docs/CI.md)): `retrofe\retrofe.exe`, `layouts\Aeon Nox` and `BUILD.txt`, without the DLLs.

`deploy_test_build.py` (run where the agents work, on Unraid) downloads that zip, checks its SHA-256 and deploys it to the PC's test drive (`\\JOHN-DESKTOP\retrofe`):

```
\\JOHN-DESKTOP\retrofe\
├── RetroFE-Bare\             a test install (its RetroFE root, per docs/FOLDERS.md)
│   ├── retrofe\retrofe.exe   always the newest build: point a shortcut here
│   ├── retrofe\BUILD.txt     which build that is
│   ├── retrofe\*.dll         RetroFE's DLLs, copied the first time
│   └── layouts\Aeon Nox\     updated with each build
├── builds\<date>-<commit>\   every build, kept
└── backups\                  files the old Install-TestBuild.ps1 replaced in E:\Storage\OneSauce
```

- On Windows, RetroFE's root is the folder above `retrofe\`, so each install folder is its own root; no `RETROFE_PATH` is needed.
- A new install folder gets RetroFE's DLLs and fonts (`Package/Environment/Windows/retrofe`) on its first deploy. After that, only `retrofe.exe` and `layouts\Aeon Nox` change; files are copied over, nothing is deleted, so whatever RetroFE, Manage and the Catalog created in the install stays.
- If RetroFE is running from an install, its `retrofe.exe` can't be replaced: that install is left as it was, and the script says so. The build is still kept in `builds\`.
- `--install NAME` (repeatable) picks the installs to update; the default is `RetroFE-Bare`. A OnesaUCE-derived setup would be another install beside it.
- The drive is reached over SMB with the `retrofe-deploy` login (`~/.config/coinops/smb.txt`: the user name, then the password), or with `--path` where it's mounted. SMB needs `pip install smbprotocol`.
