# Arquitetura do x360-native

## Política permanente de performance

**Performance-first, correctness-preserving.** Performance é objetivo central:
evitar overhead desnecessário, medir antes/depois e preservar correção,
compatibilidade e fidelidade dos originais. Otimizações compartilhadas devem
servir genericamente aos títulos. Avaliar continuamente compilador/AOT, CPU,
memória, GPU e carregamento, sem modificar semântica PPC/XAM ou enfraquecer
sincronização. Instrumentação opcional e desligada normalmente. Preservar C7/C8,
isolamento de versões, SDK pinado e patches reproduzíveis. Metodologia, limites
das métricas e roadmap: [Performance](performance.md).

Base arquitetural de 2026-10-11, revisada em 2026-10-15. Problemas do SDK
e contagens antigas são históricos; o lock/patches e [STATUS](../STATUS.md)
registram as correções e o estado consolidado. Marcadores usados:

- **[fato]** confirmado no código/execução, com referência;
- **[hipótese]** decisão de projeto ainda não demonstrada;
- **[não verificado]** comportamento esperado que nenhum teste exercitou.

Referências de código são relativas a `external/rexglue-sdk` (ReXGlue v0.10.0,
commit `f5337cdc947ff6d4c4196737e2c807a48f2a1fc2`) salvo indicação contrária.

## 1. O que a recompilação estática resolve e o que não resolve

ReXGlue traduz o código PowerPC de um XEX para C++ antes da execução; o C++ é
compilado por Clang e ligado a um runtime nativo.

- **[fato]** Não há JIT nem interpretação de instruções em tempo de execução: as
  funções recompiladas são registradas numa tabela de despacho a partir de
  `PPCFuncMappings[]` durante `Runtime::Setup()` (wiki *Runtime Architecture
  Overview*; `src/system/function_dispatcher.cpp`).
- **[fato]** O XEX continua sendo carregado em tempo de execução, mas só pelos
  **dados** (seções inicializadas, TLS, tabela de imports). O código vem do
  binário host (wiki *Kernel State & Objects*, "Code vs Data Loading").
- **[fato]** O jogo continua dependendo de um ambiente que reproduza o console:
  memória de 4 GB com layout de guest, kernel (`xboxkrnl`), XAM, sistema de
  arquivos virtual, threads, áudio (XMA), entrada e GPU Xenos. No ReXGlue esses
  serviços são implementações nativas de alto nível (HLE) derivadas do Xenia
  (`src/kernel/xboxkrnl`, `src/kernel/xam`, `src/graphics`). A GPU continua
  emulada: comandos PM4 e shaders Xenos são traduzidos para D3D12/Vulkan pelo
  plugin `rexgpu-xenos`.
- **[fato]** Cobertura de exports do kernel na v0.10.0, por contagem textual
  (`grep`, aproximada): xam 177 `REX_EXPORT` para 1735 entradas da tabela
  (1535 `REX_EXPORT_STUB`); xboxkrnl 261/922 (617 stubs); xbdm 0/255 (253 stubs).
- **[fato]** Um stub (`REX_STUB`, `include/rex/hook.h:52-56`) só registra
  `"<nome> STUB"` em nível *warning* e **não altera r3**: o guest recebe o valor
  que já estava no registrador (normalmente o 1º argumento), não "sucesso".
  O supervisor conta essas linhas em cada execução.

Consequência: "compilou" não significa "funciona". Cada título exige análise,
correções de fronteira de função, implementação de imports ausentes e validação
de comportamento. Medições posteriores e seus limites estão em
[Performance](performance.md); não há ganho global universal demonstrado.

## 2. Componentes atuais

```
x360/
├── deps.lock.json          revisões fixadas (SDK, LLVM) — fonte única
├── CMakeLists.txt          projeto raiz; inclui o SDK via add_subdirectory
├── CMakePresets.json       preset win-amd64 (espelha o windows-base do SDK)
├── external/rexglue-sdk/   submódulo ReXGlue fixado + patches reproduzíveis
├── scripts/                bootstrap (LLVM, submódulos, symlinks), devshell
├── src/supervisor/         x360n: doctor, identify, run, session
├── src/content/            inspeção e extração de conteúdo
├── src/desktop/            model/, win32/, resources/
├── src/native/             commands/, d3d12/, memory/, pipeline/, textures/, shaders/
├── cmake/                  sdk/, build/, runtime/, distribution/
├── tests/                  testes do projeto (ver README)
├── titles/                 configurações por título/versão e overrides AOT
├── docs/                   este documento, dashboard, compatibilidade
└── .local/                 inputs, estado local, build, evidence e archive (ignorados)
```

Árvore completa em [filetree.txt](filetree.txt); organização e limites em
[reorganização](reorganization.md). [Conteúdo local](guides/content-layout.md)
descreve dashboards por versão, discos e DLCs. Esses agrupamentos físicos não
alteram as fronteiras funcionais D1–D7.

### 2.1 ReXGlue (dependência)

**[fato]** Alvos usados (`src/*/CMakeLists.txt`): `rexglue` (CLI), `rexcodegen`
(estático), `rexruntime` (DLL: memória, kernel, VFS, threads), `rexgpu-xenos`
(DLL, plugin de GPU), `rexui`/`rexinput`/`rexaudio` (objetos). CLI real da
v0.10.0 (`src/rexglue/main.cpp`, `src/rexglue/commands/*.cpp`):

```
rexglue [--log-level L] [--log-file F] [-v] [-f] <comando>
  codegen [manifest.toml] [--target ...] [--ignore-stamp]
  init --project-name N --xex-path P [--project-root R] [--game-root G] [--scan-dll] [--template-dir T]
  init module --project-root R --xex-path P --guest-path G
  init achievements <xex> [out_dir] [--language N]
  recompile-tests --bin-dir B --asm-dir A --output O
```

Códigos de saída: 0 sucesso, 1 falha, 2 abortado pelo usuário (`main.cpp`).
**[fato]** A wiki (commit `ebc5cc85`, 2026-06-04) descreve flags antigas
(`--app_name`, `--app_root`) que não existem mais na v0.10.0; vale o código.

**[fato]** O codegen da v0.10.0 só aceita XEX: `project_recompiler.cpp:262,289,310`
usa `xex_module()` e grava `format = "xex"`. A menção a "ELF" na wiki não se aplica.

**[fato]** Um título pode ter vários módulos (`[[modules]]` com `guest_path`,
usados por `XexLoadImage`), criados por `rexglue init module`. Pinyon Shift usa
isso para `SpeechFacade_default.xex` e `XMediaFacade_default.xex`.

### 2.2 Supervisor `x360n`

Ferramenta de desenvolvimento por terminal. **Não é a dashboard** e não deve ser
apresentada como interface do console.

| Comando | O que faz | Validação |
|---|---|---|
| `doctor` | compara `external/rexglue-sdk` com `deps.lock.json`, verifica clang fixado, MSVC, `rexglue --version`, binutils PPC, permissão de symlink, diretórios | `env.doctor` |
| `identify <arq>` | SHA-256 + tamanho; classifica XEX2 / XEX1 / outra variante `XEX` / inválido (só o XEX2 é lido; o resto sai com o motivo); para XEX2: flags, title/media id, versões, disco, nome PE, entry point, base, formato, imports por biblioteca | testes com cabeçalhos sintéticos + 20 000 corrupções aleatórias; `local.inventory …` e `local.integration targets …` sobre `.local/inputs` |
| `run <exe>` | executa **somente** saídas de build (`.local/build`, `external/rexglue-sdk/out`), recusa `.local/inputs`; Job Object com kill-on-close, timeout, stdout/stderr em arquivo, relatório JSON com hashes do executável **e das DLLs ao lado dele**, revisões, diretório de trabalho e diagnóstico do log (stubs, mensagens XAM, aberturas que falharam, chamadas não registradas, `[error]`). O filho roda no diretório do executável. `--crash-stack`: roda o filho pela API de debug e grava a pilha (DbgHelp + PDB) de uma exceção não tratada | testes de processo e ponta a ponta com um processo-fixture |

**[fato]** O supervisor reutiliza as estruturas do SDK
(`include/rex/system/util/xex2_info.h`) e CLI11/fmt do SDK, mas não liga o
runtime. O parser do SDK (`xex_module.cpp`, `user_module.cpp:308`) usa `assert`
sobre dados confiáveis; o nosso verifica limites porque a entrada não é confiável.

## 3. Fluxo de preparação de um título

```
arquivos locais (.local/inputs)
 → identificação      x360n identify                      [implementado]
 → análise + geração  rexglue codegen <manifest>          [implementado; conteúdo fornecido localmente]
 → compilação         projeto CMake gerado por rexglue init [implementado]
 → execução           x360n run <exe recompilado>         [implementado; títulos e fixtures documentados]
 → relatório          .local/evidence/runs/<utc>-<label>/report.json [implementado]
```

Cada etapa grava a saída da ferramenta original e seu código de saída; nenhuma
etapa promove o estado da seguinte.

**[fato]** O caminho completo instrução→C++→nativo foi exercitado sem conteúdo
proprietário por (a) 1458 casos do SDK (`tests/ppc`, 166 arquivos) e (b) um
programa PowerPC nosso (`tests/ppc_smoke/asm/x360n_smoke.s`) com laço contado,
acesso big-endian à memória, `stwbrx` e chamada `bl` entre funções. Isso valida
tradução de instruções e arestas de chamada, **não** carga de XEX, imports,
threads, VFS ou GPU.

**[fato]** M1 foi demonstrado com o XEX Iruka fornecido localmente.
A biblioteca atual importa conteúdos e gera projetos AOT genéricos; perfis
manuais são overrides opcionais. [Iruka](reports/titles/validation-iruka.md),
[biblioteca e recibos](guides/universal-library.md).

## 4. Modelo de processos (dashboard ↔ jogos)

**[fato]** O código de um título é gerado e compilado antes da execução; o
executável principal liga o módulo de entrada. Módulos DLL de guest
(`[[modules]]`) viram bibliotecas nativas (`<projeto>_<alvo>.dll`) que
`KernelState::LoadUserModule` carrega quando o guest chama `XexLoadImage`
(`src/system/kernel_state.cpp:744-809`): resolve `ReXModule_Register` e
`ReXModule_GetImageInfo`, confere `image_base`/`image_size` contra o XEX carregado
e registra as funções. Só entram módulos presentes no registro gerado pelo
codegen (`RegisterRecompiledModule`, template
`resources/templates/codegen/module_registry_cpp.inja`; `project_recompiler.cpp:500`).
Ou seja, há carga de código nativo em tempo de execução, mas restrita ao que foi
preparado no build; um XEX desconhecido não executa.

**[fato, patch 0019]** `XamLoaderLaunchTitle(path, flags)` grava o pedido e chama
`KernelState::TerminateTitle()`; `path == NULL` registra retorno à dashboard.
`XamLoaderTerminateTitle` registra encerramento de título separadamente.
`XamLoaderLaunchTitleEx`,
`...ForReason` e `...OnDvd` são stubs (`xam_misc.cpp:537-539`).

**[fato, 2026-10-12]** Dashboard e jogos rodam como processos separados.
`x360n session <manifest>` recebe o registro binário emitido pelo hook
OnGuestThreadExit, preserva flags/launch data e resolve o destino por caminho
e SHA-256 contra os perfis permitidos no manifesto da dashboard. Não segue
crash/timeout. Pedido de retorno ou término do título retorna ao perfil inicial;
retorno simples da thread é identificado como política do supervisor. Fechamento
da janela encerra a plataforma. Ciclo original 17559 → Castle Crashers → 17559
confirmado pelo usuário (docs/dashboard.md §0.19), com NULL/flags 0x100 emitidos
pelo jogo. Flags e launch data chegam ao perfil receptor, que traduz o contexto
Arcade da 17559 conforme seu XAM; outras versões não herdam essa tradução.
A intro só ocorre na entrada inicial. A sessão mantém um frame Win32 e superfícies filhas SDL
reais por título; --separate-windows preserva o diagnóstico. Fechar o frame encerra
a plataforma. GuestExit ainda não prova retorno pedido pelo Guide.

Atualização: perfis locais persistentes demonstrados no C7.7; Guide/Xbox Home
no ciclo Forza. Guide completo e saves comerciais continuam pendentes.
Os user_data_root anteriores foram preservados separadamente (sem migrar saves).

Alternativa registrada, não escolhida: um único processo que carregaria títulos
como bibliotecas, aproveitando o mecanismo de módulos acima. O runtime tem um
único módulo executável por `KernelState` e o título ocupa o espaço de imagem de
usuário (o pipeline de testes usa 0x82010000); trocar de título exigiria
desmontar e recriar a memória de guest dentro do processo. Se a dashboard ocupa a
mesma região não foi verificado.

A alternativa no mesmo processo não foi implementada; ver `docs/dashboard.md`.

## 5. Identidade de uma recompilação

**[fato, implementado]** O relatório de execução registra: SHA-256 e tamanho do
executável, commit do SDK lido do submódulo e o fixado no lock, commit do
projeto (nulo enquanto não houver commit), versão do supervisor e configuração.

**[fato, recibos C2]** A identidade de um título preparado inclui a
tupla (SHA-256 de cada XEX de entrada, versão/base_version/media_id do cabeçalho,
commit do SDK, versão do LLVM, hash do manifesto e das inclusões `[functions]`).
Configurações por título ficam em `titles/` e são indexadas pelo SHA-256 do XEX,
não pelo title id: regiões, atualizações (`patch_delta` em XEXP) e mídias
diferentes têm executáveis diferentes. Pinyon Shift faz o mesmo
(`config/supported-dumps.json`: `exact_sha256_and_size`, dumps desconhecidos rejeitados).

## 6. Decisões

| # | Decisão | Motivo | Reversão |
|---|---|---|---|
| D1 | Fixar ReXGlue **v0.10.0** (`f5337cdc`) | última versão estável (2026-08-21); `development` muda diariamente; Pinyon Shift parte da mesma versão | trocar o commit em `deps.lock.json` e no submódulo |
| D2 | LLVM **22.1.8** portátil em `.local/toolchains` | o SDK exige Clang ≥ 18 (`CMakeLists.txt:83-93`) e o CI usa LLVM upstream (`choco install llvm`); 22.1.8 era a última estável na data da tag; sem admin nem PATH global | apagar o diretório; ajustar o lock |
| D3 | SDK via `add_subdirectory` (modo "Git Submodule" da wiki), sem `install` | `install` executa `reg add HKCU\Software\Kitware\CMake\Packages\rexglue` (`cmake/rexglue_install.cmake:194-217`) | rodar o install quando o usuário autorizar |
| D4 | Backport do commit upstream `b5e0cf8` no nosso CMake, só para o pin v0.10.0 | `include/rex/hash.h` (público) inclui `<xxhash.h>`, mas xxHash é ligado como PRIVATE; `unit_tests` não compila | removido automaticamente ao trocar o pin |
| D5 | Materializar 15 symlinks do libmspack como cópias verificadas (`scripts/setup/materialize-symlinks.ps1`) | sem Developer Mode a conta não cria symlinks; o git os grava como texto e `lzxd.c` não compila | `git -C external/rexglue-sdk/thirdparty/libmspack checkout -- .` |
| D6 | O supervisor não liga o runtime | identificar e supervisionar não precisa de memória de guest; a DLL do runtime acoplaria o supervisor a cada título | ligar quando houver necessidade demonstrada |
| D7 | Teste PowerPC próprio pelo pipeline oficial `recompile-tests` | único caminho do SDK que recompila e executa código PPC sem XEX | — |

## 7. Problemas conhecidos no SDK v0.10.0 (observados aqui)

1. **Versão errada em modo submódulo**: `rex_resolve_version()` usa
   `CMAKE_SOURCE_DIR` (`cmake/rex_version.cmake`), que é o nosso repositório;
   resultado `0.10.0.0-dev.unknown`. Se este repositório tiver uma tag `vX.Y`
   maior que 0.10, o configure do SDK aborta. Correção sugerida upstream: passar
   `SOURCE_DIR ${CMAKE_CURRENT_SOURCE_DIR}`.
2. **`unit_tests` não compila** sem o backport D4 (corrigido upstream em `b5e0cf8`).
3. **Teste `unit.Depfile escapes characters make treats as syntax` falha**: o
   literal `"has\ space.toml"` (`tests/unit/codegen/output_stamp_test.cpp:227-228`)
   é um escape desconhecido; o clang o compila como `"has space.toml"`. A
   implementação está correta (`sdkcheck.*` prova). Ainda presente em `development`.
4. **4 testes `BitStream Write`** são `SKIP` upstream ("doesn't byte-swap on store").
5. `catch_discover_tests(... PROPERTIES LABELS "ppc;instructions" TIMEOUT 60)` em
   `tests/ppc/CMakeLists.txt` achata a lista de labels e o `TIMEOUT 60` não é aplicado.
6. **[hipótese]** Flags diferentes entre testes e títulos: a suíte PPC do SDK
   compila com `-ffp-model=strict -fno-strict-aliasing` (escopo do SDK), mas o
   template de `rexglue init` só adiciona `-msse4.1`/`-fasync-exceptions`
   (`resources/templates/init/rexglue_cmake.inja:40-47`). Diferenças de ponto
   flutuante/aliasing entre o que é testado e o que um título compila são
   possíveis; não demonstradas.
7. **Mensagem enganosa do `codegen`** para manifesto ilegível: `IsManifest`
   devolve `false` para qualquer erro de leitura (`src/codegen/manifest.cpp:153-160`),
   então um caminho inexistente vira `… is not a manifest: no [project] section`.
8. **Ordinais do `xam` além da tabela**: a tabela de exports termina em `0x545`
   no trecho `0x53x–0x54x`; a dashboard 17559 importa `0x546–0x549` e `0x54B`
   (`docs/dashboard.md` §0.2). O codegen registra `Cannot resolve ordinal` e segue.
9. *(ambiente, não SDK)* O LLVM 22.1.8 fixado em `deps.lock.json` **não tem o
   alvo PowerPC** (`llvm-mc: unable to get target for 'powerpc64-unknown-unknown'`);
   não serve para desmontar imagens. `x360n-content disasm` usa o decodificador
   binutils do próprio SDK (`thirdparty/disasm`, dialeto Xenon do codegen).
10. **Destino só de `b`, sem `.pdata`, não vira função**: a análise promove
    destinos de `bl`, mas um código alcançado apenas por `b` (chamada de cauda,
    thunk) e sem entrada no `.pdata` gera `UnresolvedCall`. Na dashboard 17559,
    dos 65 stubs de duas tabelas, todos os que têm chamador `bl` foram aceitos e
    todos os só-`b` foram reportados (`docs/dashboard.md` §0.4). Os erros aparecem
    em cadeia: um alvo só é verificado quando a função que o chama passa a existir.
11. **Tabela de saltos com limite verificado antes de uma cópia do índice**:
    `cmplwi r4,22; …; mr r25,r4; lbzx r0,tabela,r25` não foi reconhecido como limite;
    a detecção leu 512 entradas e invadiu a função seguinte
    (`check-pdata-overlap.ps1` acusa; corrigido com `[[switch_tables]]`).
12. **Desvio condicional para fora de qualquer código** é registrado só na fase
    Write (`Unresolved conditional branch …`) e vira `REX_FATAL` no ponto do desvio;
    a validação não o conta como erro.
13. **Import sem ordinal na tabela não tem definição**: `Cannot resolve ordinal`
    registra o thunk como `sub_<endereço>`, o código gerado o chama e nada o
    define — o título não linka. A dashboard resolve no próprio título com
    armadilhas (`titles/dash_17559/src/unresolved_imports.cpp`).
14. **Fontes de kernel fora do build**: `src/kernel/CMakeLists.txt` deixa
    comentados `xboxkrnl_usbcam.cpp`, `cert_monitor.cpp` e `debug_monitor.cpp`
    (iguais no `development`); os 11 `XUsbcam*` ficam sem símbolo. Um título que
    os importe não linka. `scripts/analysis/xex-imports.ps1` só conta fontes compiladas.
15. **Tabela de funções sempre no heap `v80000000`** — *corrigido localmente pelo
    patch 0001*: `Memory::InitializeFunctionTable` (`src/system/xmemory.cpp:787-806`
    no original) a alocava em `image_base + image_size` no heap
    `0x80000000–0x8FFFFFFF`. Imagens acima de `0x90000000` (dashboard 17559 em
    `0x92000000`) faziam o setup falhar com `C0000001`. O endereço é contrato com o
    código gerado (`REX_LOOKUP_FUNC`, `_indirect_call.inja:19-20`) e não muda; o
    patch escolhe o heap XEX que contém a tabela inteira e valida o pedido.
16. **Falha de setup derrubava o encerramento** — *corrigido localmente pelo patch
    0002*: `Runtime::Setup` chamava `Shutdown()` ao falhar, destruindo o sistema
    gráfico antes de o `ReXApp` liberar o drawer, a textura de fonte do ImGui e o
    presenter feitos com ele; `ReXApp::OnDestroy` caía em violação de acesso no
    `D3D12Core` (pilha em `docs/dashboard.md` §0.6).
17. **Cópia da DLL só ao relinkar**: `rexglue_setup_target` copia `rexruntime` e o
    plugin de GPU no POST_BUILD do executável (`cmake/rexglue_helpers.cmake:104-110,
    159-163`). Mudando só a DLL, o executável não relinka e a cópia ao lado dele
    fica velha — aconteceu com o teste do patch 0002. `x360n_stage_runtime_dlls()`
    copia antes de cada build do executável, também com `--target` (a primeira
    versão só copiava num build completo, e um build de um alvo só deixou a DLL
    velha ao lado do teste do patch 0003); o `x360n run` grava o SHA-256 das DLLs usadas.
18. **Import registrado no endereço 0** — *corrigido localmente pelo patch 0003*:
    o carregador pareava slot de valor e thunk de um import só dentro de um registro
    de biblioteca (`XexModule::SetupLibraryImports`). Com o slot num registro e o
    thunk em outro (dashboard 17559: `xam 0x8E2`), a metade sem thunk virava símbolo
    em `thunk_address = 0` (`PopulateBinaryData`); sendo função no SDK, o codegen a
    punha em `PPCFuncMappings[0]` como `{ 0x0, … }`, e o runtime, que para no primeiro
    `guest == 0`, registrava **zero** funções. O patch completa a entrada da mesma
    biblioteca e faz do import-função sem nenhum thunk um erro de validação
    (`ImportWithoutThunk`) em vez de função no endereço 0.
19. **Saída normal destruía a janela antes do runtime** — *corrigido localmente pelo
    patch 0004*: `ReXApp::OnDestroy` fazia `window_.reset()` antes de
    `runtime_.reset()`; `~MnkInputDriver` (`DetachFromWindow`) usava a janela já
    destruída, que a SDL destrói sem `OnClosing` aos ouvintes
    (`src/ui/window_sdl.cpp:131-134`). Só depois de um setup bem-sucedido (o driver
    recebe a janela por `AttachWindow`). Agora o runtime sai antes, com a janela
    viva e sem nada feito do device gráfico pendurado. O `rex_app.cpp` é compilado
    em cada executável: o título precisa ser recompilado.
20. **Carimbo do codegen não inclui o próprio codegen**: a impressão digital
    (`src/codegen/output_stamp.cpp:67-96`) usa versão do SDK, flags e conteúdo das
    entradas. Um `rexglue` corrigido pula a geração ("up to date"); regenerar com
    `codegen --ignore-stamp` (não `--force`, que também desliga a validação).
21. **`KeGetImagePageTableEntry` com bit 0 = paridade do índice da página** —
    *corrigido localmente pelo patch 0005*: a fórmula veio do Xenia antes do commit
    `331ddf79bf96`, que liga o bit 0 nas páginas do heap XEX de 4 KB. A dashboard
    17559 exige esse bit para os buffers dos seus trechos Lua (`KeBugCheck(0xF7)`).
    O valor continua uma aproximação do que o hypervisor devolve.
22. **Funções só alcançadas por ponteiro, sem `.pdata`, não são geradas**: o
    `VTableScanner` (`src/codegen/vtable_scanner.cpp`) só acha vtables pelo RTTI
    (Complete Object Locator) e o scan geral de ponteiros está desligado
    (`src/codegen/analyze.cpp`, "too many false positives"). Thunks de ajuste e
    folhas de vtables sem RTTI ficam fora, e a chamada falha com `Call to invalid or
    unregistered function`. Castle Crashers teve 1; a dashboard 17559, 515.
    `scripts/analysis/check-pointer-targets.ps1` lista os candidatos com critérios
    verificáveis; as entradas vão para a análise do título, não para o SDK.
23. **Página de sistema em endereço fixo `0x8E038600`** — *não resolvido*: a
    dashboard lê sinalizadores em `0x8E038614`, `0x8E038630` e `0x8E038678` por
    endereço absoluto. O SDK não mapeia nada ali (violação de acesso); o Xenia
    aloca a página zerada com "Data is currently unknown". Conteúdo real desconhecido.

Patches locais (base `f5337cdc`): `patches/rexglue-sdk/README.md`; aplicação
reproduzível por `scripts/setup/apply-sdk-patches.ps1` e verificação no CMake.

## 8. Pinyon Shift: o que é reutilizável

Lido em `arcanite24/pinyon-shift@cb26a0fe` (2026-10-07). É um port de um único
título (Forza Horizon, title id `4D5309C9`) sobre **ShiftGlue**, um fork do
ReXGlue fixado em `0038a402`.

| Reutilizável (padrão, não código) | Específico do título |
|---|---|
| Identidade exata do dump: SHA-256 + tamanho de ISO e de cada XEX; dump desconhecido é rejeitado; entrada original nunca modificada (`config/supported-dumps.json`) | renderizador nativo próprio que substituiu o Xenos do SDK (`docs/DEVELOPMENT.md`; tag `xenos-rollback`) |
| Manifesto com `[entrypoint]` e `[[modules]]` por `guest_path`, apontando para `.local/` (`config/rexglue/pinyon_shift_manifest.toml`) | plugin de GPU `fh1` e código do jogo **dentro do fork do SDK** (`src/graphics/fh1_shader_pack.cpp`) |
| SDK por `add_subdirectory(... EXCLUDE_FROM_ALL)` a partir do fonte fixado (`cmake/PinyonShiftRexGlue.cmake:102-137`) | `[functions]`/hooks em `config/rexglue/analysis/*.toml`, trainer, DLL de DLC, saves `ForzaProfile` |
| Exclusão do `*_register.cpp` gerado, já que `Runtime::Setup` registra via `PPCFuncMappings` (`PinyonShiftRexGlue.cmake:175-177`) | rotas roteirizadas `fh1-*`, medições de FPS e ferramentas `tools/*fh1*` |
| Testes reproduzíveis a partir de uma semente de save copiada; nunca escrever no save real (`AGENTS.md`) | launcher WPF próprio |
| Comparações só com hashes de binário, SDK e configuração congelados | — |
| Opções do template do SDK para o código gerado, como `-fasync-exceptions` (`PinyonShiftRexGlue.cmake:202`) | — |

Conclusão: o que se aproveita é a disciplina de identidade, de layout local e de
integração com o SDK. O renderizador e as correções vivem no fork e no jogo;
não são uma solução geral, e não os tratamos como tal.

## 9. Fora do escopo atual

Xbox Live, multiplayer online, Kinect e periféricos especiais. Títulos que
dependem desses recursos não serão classificados como totalmente suportados.
Xenia é apenas referência de comportamento; não é usado como executor, e não há
JIT ou interpretador no caminho principal.
