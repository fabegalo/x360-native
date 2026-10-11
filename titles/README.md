# titles/

Configurações e correções por título **e versão de executável**. Cada pasta é um
projeto gerado por `rexglue init` (v0.10.0) com as adaptações do x360-native
(backport do SDK, plugin de GPU, preset com o LLVM fixado) e é compilada por
`scripts/build-title.ps1 -Title <nome>`.

| Pasta | Executável de entrada (SHA-256) | Documento |
|---|---|---|
| `iruka/` | `default.xex` `66ec34f2…cb61e31` (amostra XDK, `rexglue/demo-iruka`) | `docs/validation-iruka.md` |

Em cada pasta:

```
<nome>_manifest.toml   entrada do rexglue codegen (caminhos para .local/inputs)
analysis/<nome>.toml   [functions], [[switch_tables]] etc.; o cabeçalho cita o
                       SHA-256 do XEX, e cada entrada a evidência (desmontagem,
                       log do codegen ou da execução)
src/                   app do título e correções específicas desse executável
generated/rexglue.cmake  vem do template do SDK (versionado)
generated/<módulo>/    código gerado a partir do XEX (ignorado pelo git)
```

A identidade é sempre o SHA-256 do XEX (`x360n identify --json`); regiões,
atualizações e mídias diferentes têm executáveis diferentes. Nenhum arquivo de
jogo ou de sistema entra aqui: os XEX ficam em `.local/inputs`, o build em
`.local/build/titles/<nome>`.
