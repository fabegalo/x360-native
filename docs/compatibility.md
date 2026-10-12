# Matriz de resultados

Etapas: **análise → geração → compilação → inicialização → menu → gameplay →
save/load → conclusão**. Valores: `ok` (demonstrado pelo teste citado),
`falha`, `—` (não se aplica ao tipo de alvo), `não testado`.
Um título só avança de etapa com evidência registrada; "compilou" não implica
"inicia", e "inicia" não implica "jogável".

Ambiente de todas as linhas abaixo: Windows 11 Pro 10.0.26300, AMD Ryzen 7
5700X3D, 40 GB RAM, RTX 4070; LLVM/Clang 22.1.8 + MSVC 14.44 (VS Build Tools
17.14.41), Windows SDK 10.0.26100; configuração Debug.
Revisão do projeto: commits locais de 2026-10-12; SDK `f5337cdc` (v0.10.0) com os
patches locais 0001–0010 (`patches/rexglue-sdk/`).
Títulos compilados em `RelWithDebInfo` (ver `docs/validation-iruka.md`).

## Títulos e amostras

| Alvo | Identidade | Análise | Geração | Compilação | Inicialização | Menu | Gameplay | Save/load | Conclusão | Teste | Limitações |
|---|---|---|---|---|---|---|---|---|---|---|---|
| Amostra XDK "Dolphin" (`rexglue/demo-iruka@4d81580`) | `default.xex` SHA-256 `66ec34f2…cb61e31`, 2 658 304 bytes; sem title id | ok com os ajustes da demo (`titles/iruka/analysis/iruka.toml`); sem eles, falha em execução | ok (77 arquivos, 5222 funções, determinística) | ok com backport `b5e0cf8` | ok: renderiza e anima por 30–40 s sem crash | — (sem menu) | — (não interativa) | — | — | `docs/validation-iruka.md`; runs `20261011T070446Z`, `20261011T070909Z` | entrada, áudio, encerramento normal, execução longa e Release não validados |
| Castle Crashers (XBLA, `584108B7`, v0.0.1.3) — experimental | `default.xex` SHA-256 `825e9c8c…f5d9ad` (pacote LIVE `ed7b65a8…c31d41e74`) | ok com 3 ajustes (`titles/castle_crashers/analysis/castle_crashers.toml`) | ok (41 arquivos, 3714 funções) | ok | ok: 60 s sem falha, 0 stubs | ok: menu principal renderizado (captura); navegação por teclado e controle **relatada pelo usuário** | **relatado pelo usuário** (teclado e controle); não observado por nós | não testado | não testado | `docs/validation-castle-crashers.md`; run `20261011T171213Z-cc-run-2` | provável modo trial (`license_mask` 0); 2 shaders `.xpu` ausentes no pacote; sem save/load, Live fora do escopo |

## Dashboard

Estado atual (§0.13–0.14): 17559 usa a fonte original e a intro AOT original,
com som/imagem confirmados e retorno natural à dashboard em cerca de 11 s.
My Games recebe a URI expandida e abre uma tela sem conteúdo; a enumeração
de jogos ainda não foi observada e o ciclo com Castle Crashers não ocorreu.
23/23 sdkcheck, iruka L1–L5 e execução separada do Castle Crashers por 45 s
validaram esse incremento (duas chamadas ao stub AchievementsUI no jogo).
Retail 6717 tem perfil separado: apresenta quadros, troca de blade e move o
destaque por entrada roteada (run `20261012T054645Z-dash6717-input`); sem texto
(`XenonJKLatin.xtt` da 6717 ausente); lançamento não testado. `docs/dashboard.md` §0.15.

Os parágrafos seguintes preservam os resultados históricos anteriores.

Atualização 2026-10-12: **primeiro quadro real renderizado**, com blocos e ícones,
e mudança de painel durante entrada (`20261012T030938Z-dash-jmp-fixed`). O patch
0007 executa o DPC que libera novas submissões; a câmera retorna erro explícito
de indisponibilidade e os endereços de setjmp/longjmp foram configurados. A
espera anterior lia conclusão `0x0B` para alvo `0x0D`; zero era erro da sonda
externa, corrigido. Nenhum contador foi forçado. Ver `docs/dashboard.md` §0.11.

Incremento de navegação (§0.12): textos confirmados pelo usuário usando a fonte
alternativa original; controle e Shift+setas respondem, menu do sistema abre.
Fila MNK e ABI de data/hora corrigidos; strings comuns ausentes agora retornam
nulo. My Games emite `gameslibrary:_RootTitle=My Games;`, mas XamLaunchURI não
tem roteador e retorna E_NOTIMPL. Biblioteca e ciclo com Castle Crashers não
funcionam. Algumas páginas do sistema ainda leem 0x1C após XConfig categoria 7.
`media:\SegoeXbox-Light.xtt` continua ausente; hipótese de região de 64 KiB
zerada inalterada. Validação: 22/22 sdkcheck, iruka L1–L5, Castle Crashers com
menu principal observado em 45 s; este último foi executado separadamente.

A linha abaixo preserva o resultado histórico anterior a essas correções:

| Versão | Identidade | Análise | Geração | Compilação | Inicialização | Teste | Limitações |
|---|---|---|---|---|---|---|---|
| 2.0.17559.0 (Metro), `FFFE07D1` | `dash.xex` SHA-256 `3ad8f38d…14f927`, 5 971 968 bytes (`dashbigger.xex` `fc4ad906…ebc8c2` tem a mesma imagem carregada) | ok com 30 entradas conferidas uma a uma e 1 tabela de saltos (`titles/dash_17559/analysis/dash_17559.toml`) + 515 inícios só por ponteiro classificados por script (`dash_17559_pointer_targets.toml`) | ok (39 141 funções; 0 sobreposições `.pdata`; sem `--force`) | ok; 12 armadilhas e `XUsbcamGetState` implementado no título; exe `cec26d64…40bc12` | **parcial**: com os patches 0001–0005, 39 141 funções registradas e **o código executa** a partir de `xstart` (áudio, idioma, fontes, perfil, abre `shrdres.xzp`); para ~2 s depois lendo a página de sistema fixa `0x8E038630`, não mapeada (conteúdo desconhecido). Nenhum quadro apresentado | `docs/dashboard.md` §0.8; runs `…dash17559-codegen-10`, `20261012T012216Z-dash17559-run-10`, `…run-9-crashstack` | 28 stubs chamados (não definem r3); `DASHUSER:` ausente; menu, navegação, lançar jogo: não testados |

## Programas de teste sem conteúdo proprietário

Não são jogos. Validam o caminho instrução→C++→nativo, não o sistema.

| Alvo | Identidade | Análise | Geração | Compilação | Execução | Teste | Limitações |
|---|---|---|---|---|---|---|---|
| ReXGlue `tests/ppc` (166 arquivos .s) | fontes do SDK em `f5337cdc` | ok | ok | ok | ok: 1458/1458 casos | `ctest -L ppc` | binário bruto em 0x82010000, sem XEX, imports, threads, VFS, GPU |
| x360n `tests/ppc_smoke/asm/x360n_smoke.s` | `.bin` SHA-256 `8eb71d00…5219a` (120 bytes) | ok | ok (5 funções, 2 `bl` resolvidos) | ok | ok: 4/4 casos, 12 asserções; via `x360n run`, saída 0, 0 linhas STUB | `ctest -R "x360n[.]ppc"` | idem; o relatório da execução guarda o SHA-256 do executável testado |

O SHA-256 do `.bin` foi conferido com `sha256sum` e `Get-FileHash`.
