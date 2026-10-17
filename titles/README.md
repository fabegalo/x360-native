# titles/

Configurações e correções por título **e versão de executável**. Cada pasta é um
projeto gerado por `rexglue init` (v0.10.0) com as adaptações do x360-native
(backport do SDK, plugin de GPU, preset com o LLVM fixado) e é compilada por
`scripts/build-title.ps1 -Title <nome>`.

| Pasta | Executável de entrada (SHA-256) | Documento |
|---|---|---|
| `iruka/` | `default.xex` `66ec34f2…cb61e31` (amostra XDK, `rexglue/demo-iruka`) | `docs/validation-iruka.md` |
| `castle_crashers/` | `default.xex` `825e9c8c…f5d9ad` (XBLA `584108B7`, v0.0.1.3) | `docs/validation-castle-crashers.md` |
| `motocross_madness/` | `default.xex` `2a8406bc…5dcd` (XBLA `58410B0F`, v0.0.0.3) | `docs/validation-motocross-madness.md` |
| `forza_horizon/` | `default.xex` `db40df60…9e3987c` (4D5309C9, v0.0.0.10) | `docs/validation-forza-horizon.md` |
| `dash_6717/` | `dash.xex` `f8a51ea3…cb87d8` (2.0.6717.0; diagnóstico) | `docs/dashboard.md` |
| `dash_17559/` | `dash.xex` `3ad8f38d…14f927` (dashboard 2.0.17559.0) | `docs/dashboard.md` |

O desktop também gera projetos genéricos em `.local/aot/`; os diretórios aqui
são perfis manuais opcionais, não requisito para importar novos jogos.

Em cada pasta:

```
<nome>_manifest.toml   entrada do rexglue codegen (caminhos para .local/inputs)
analysis/<nome>.toml   [functions], [[switch_tables]] etc.; o cabeçalho cita o
                       SHA-256 do XEX, e cada entrada a evidência (desmontagem,
                       log do codegen ou da execução)
analysis/<nome>_*.toml entradas geradas por script (ex.: dash_17559_pointer_targets.toml,
                       scripts/analysis/check-pointer-targets.ps1), separadas das conferidas
                       uma a uma; incluídas pelo manifesto
src/                   app do título e correções específicas desse executável
generated/rexglue.cmake  vem do template do SDK (versionado)
generated/<módulo>/    código gerado a partir do XEX (ignorado pelo git)
```

A identidade é sempre o SHA-256 do XEX (`x360n identify --json`); regiões,
atualizações e mídias diferentes têm executáveis diferentes. Nenhum arquivo de
jogo ou de sistema entra aqui: os XEX ficam em `.local/inputs`, o build em
`.local/build/titles/<nome>`.

Para adicionar arquivos de um novo jogo, use
`.local/inputs/titles/<nome>/source/` para o pacote original e
`.local/inputs/titles/<nome>/extracted/` para o XEX e todos os recursos extraídos.
Hashes e identificação ficam em `metadata/`, ao lado de `extracted/`. Veja
[Onde colocar os arquivos](../docs/BUILDING.md#onde-colocar-os-arquivos). Colocar arquivos ali não cria
automaticamente um projeto AOT nesta pasta.
