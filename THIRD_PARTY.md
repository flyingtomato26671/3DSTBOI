# Dependencies

- [ctrtool 1.3.0](https://github.com/3DSGuy/Project_CTR/releases/tag/ctrtool-v1.3.0): extraction, decryption, and verification.
- [makerom 0.19.0](https://github.com/3DSGuy/Project_CTR/releases/tag/makerom-v0.19.0): application, manual, and CIA rebuilding.
- [OpenSSL](https://github.com/openssl/openssl): SHA-256 on Linux. Windows uses system BCrypt instead.
- [Minecraft Old 3DS Patcher](https://github.com/s4ammy/3DSMinecraft): shared MIT-licensed utilities, process handling, and CIA pipeline. Its copyright notice is retained in [LICENSE](LICENSE).

`cmake/FetchTools.cmake` checks pinned utility archive hashes. Keep the `tools` folder beside the patcher, or use `--tools-dir`. A source build's `cmake --install` does not copy the utilities automatically.

Release packages include the utilities, their upstream source archives under `tools/sources`, and notices under `licenses`. Makerom's bundled BLZ code uses GPLv3 or later; the complete tagged source and GPL text accompany the binary. Preserve these when redistributing the packages.

Optional payload verification uses Python 3 and LLVM 22.1.8. Normal patcher builds do not need an ARM compiler.

Dependencies retain their own licences. The patcher's MIT licence does not cover Isaac or these tools.
