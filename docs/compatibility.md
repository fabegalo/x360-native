# Matriz de resultados

Etapas: **análise → geração → compilação → inicialização → menu → gameplay →
save/load → conclusão**. Valores: `ok` (demonstrado pelo teste citado),
`falha`, `—` (não se aplica ao tipo de alvo), `não testado`.
Um título só avança de etapa com evidência registrada; "compilou" não implica
"inicia", e "inicia" não implica "jogável".

Ambiente de todas as linhas abaixo: Windows 11 Pro 10.0.26300, AMD Ryzen 7
5700X3D, 40 GB RAM, RTX 4070; LLVM/Clang 22.1.8 + MSVC 14.44 (VS Build Tools
17.14.41), Windows SDK 10.0.26100; configuração Debug.
Revisão do projeto: árvore de trabalho sem commit (2026-10-11); SDK `f5337cdc` (v0.10.0).
Títulos compilados em `RelWithDebInfo` (ver `docs/validation-iruka.md`).

## Títulos e amostras

| Alvo | Identidade | Análise | Geração | Compilação | Inicialização | Menu | Gameplay | Save/load | Conclusão | Teste | Limitações |
|---|---|---|---|---|---|---|---|---|---|---|---|
| Amostra XDK "Dolphin" (`rexglue/demo-iruka@4d81580`) | `default.xex` SHA-256 `66ec34f2…cb61e31`, 2 658 304 bytes; sem title id | ok com os ajustes da demo (`titles/iruka/analysis/iruka.toml`); sem eles, falha em execução | ok (77 arquivos, 5222 funções, determinística) | ok com backport `b5e0cf8` | ok: renderiza e anima por 30–40 s sem crash | — (sem menu) | — (não interativa) | — | — | `docs/validation-iruka.md`; runs `20261011T070446Z`, `20261011T070909Z` | entrada, áudio, encerramento normal, execução longa e Release não validados |
| Castle Crashers (XBLA, `584108B7`, v0.0.1.3) — experimental | `default.xex` SHA-256 `825e9c8c…f5d9ad` (pacote LIVE `ed7b65a8…c31d41e74`) | ok com 3 ajustes (`titles/castle_crashers/analysis/castle_crashers.toml`) | ok (41 arquivos, 3714 funções) | ok | ok: 60 s sem falha, 0 stubs | ok: menu principal renderizado (captura); navegação por teclado e controle **relatada pelo usuário** | **relatado pelo usuário** (teclado e controle); não observado por nós | não testado | não testado | `docs/validation-castle-crashers.md`; run `20261011T171213Z-cc-run-2` | provável modo trial (`license_mask` 0); 2 shaders `.xpu` ausentes no pacote; sem save/load, Live fora do escopo |

## Dashboard

| Versão | SHA-256 | Etapas | Observação |
|---|---|---|---|
| — | — | não testado | nenhum arquivo disponível; ver `docs/dashboard.md` |

## Programas de teste sem conteúdo proprietário

Não são jogos. Validam o caminho instrução→C++→nativo, não o sistema.

| Alvo | Identidade | Análise | Geração | Compilação | Execução | Teste | Limitações |
|---|---|---|---|---|---|---|---|
| ReXGlue `tests/ppc` (166 arquivos .s) | fontes do SDK em `f5337cdc` | ok | ok | ok | ok: 1458/1458 casos | `ctest -L ppc` | binário bruto em 0x82010000, sem XEX, imports, threads, VFS, GPU |
| x360n `tests/ppc_smoke/asm/x360n_smoke.s` | `.bin` SHA-256 `8eb71d00…5219a` (120 bytes) | ok | ok (5 funções, 2 `bl` resolvidos) | ok | ok: 4/4 casos, 12 asserções; via `x360n run`, saída 0, 0 linhas STUB | `ctest -R "x360n[.]ppc"` | idem; o relatório da execução guarda o SHA-256 do executável testado |

O SHA-256 do `.bin` foi conferido com `sha256sum` e `Get-FileHash`.
