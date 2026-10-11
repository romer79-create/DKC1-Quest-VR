# Contributing

Thank you for helping DKC1 Quest VR, a project of Sylar.soluciones.

1. Discuss a focused change in an issue. Use English or Spanish.
2. Work from the pinned upstream revisions. Keep the original game behavior and native center as oracles; unsupported scenes must fall back without inventing side art.
3. Keep ROMs, game-generated C/headers, assets, binary cores/APKs, captures, saves and secrets external to this repository. Do not attach them to issues, PRs or releases. Describe how another owner can reproduce locally instead.
4. Run `python scripts/check_public_source.py --tracked` and relevant synthetic tests. State hardware, versions, exact scope and limitations. Do not claim gameplay or full-campaign validation from synthetic rendering tests.
5. Submit a pull request with the problem, the resulting behavior and relevant validation. Include license/provenance for third-party code and keep original notices.

Do not change the shared guest initializer, streaming, collision or object activation to solve a presentation issue. Such changes need their own native behavior evidence and promotion gates from DKC1Recomp.

Contributions to our original code use its MIT license. Engine and metadata retain their separate licensing conditions; do not import proprietary artwork or code you cannot redistribute. Public CI runs ROM-free source checks only; generated game builds stay local.
