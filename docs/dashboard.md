# Dashboard original: diagnóstico inicial

Estado em 2026-10-11. **A dashboard original não foi analisada nem executada.**

## 0. Primeiro bloqueio real (2026-10-11)

**Não há nenhum arquivo da dashboard disponível.** `.local/inputs/dashboard/`
não existe; em `.local/inputs/titles/` há só a demo `iruka` e uma pasta de jogo
XBLA que não faz parte desta tarefa e não foi aberta. Por isso a análise e a
geração de código da dashboard **não foram tentadas**: não existe entrada.

O que o fluxo já atende, demonstrado no `iruka` (`docs/validation-iruka.md`):
identificar o XEX, `rexglue init`, codegen sem `--force`, build do título com o
SDK fixado, execução supervisionada com GPU, e regressão automática.

Arquivos necessários para a primeira etapa (análise + geração), de **uma** versão:

| Arquivo | Para quê | Evidência de que é necessário |
|---|---|---|
| `dash.xex` | executável da dashboard: entrada do `rexglue init`/`codegen` | nomeado como o executável da dashboard no Xenia Canary (`kernel/xam/user_settings.h:156`); title id esperado `FFFE07D1` (`kernel/title_id_utils.h:21`) |
| `xam.xex` (ou `$flash_xam.xex`) da **mesma versão de sistema** | não é necessário para gerar o código de `dash.xex`; é necessário para **executar**: para títulos de sistema o Xenia carrega o `xam.xex` real como módulo (`emulator.cc:630-634`), e os recursos do XAM (`gamercrd` etc.) estão dentro dele (`kernel/xam/xam_info.cc:220-235`) | idem |
| módulos e recursos que `dash.xex` abrir em tempo de execução | carregamento com `XexLoadImage` e localizadores `.xzp`/`section://` | **não dá para listar sem o arquivo**: os imports estáticos saem do cabeçalho (`x360n identify`), os carregamentos dinâmicos só da análise/execução. Não nomeamos esses arquivos antes de ver o `dash.xex` |

Onde colocar: `.local/inputs/dashboard/<versão>/` (ignorado pelo git), com os
arquivos como estão no sistema de arquivos do console de origem. Somente
arquivos que o usuário tem direito de usar; o projeto não os baixa.

Assim que existirem, o experimento D1 (§6) roda com os mesmos passos do `iruka`.

Referências: ReXGlue v0.10.0 `f5337cdc` (caminhos relativos a
`external/rexglue-sdk`) e Xenia Canary `04f85416e0f8` (consultado só como
referência de comportamento; não é usado como executor).

## 1. Por que isto é um alvo próprio

Executar a dashboard são três problemas diferentes:

1. **recompilar e iniciar o binário** (`dash.xex` e os módulos que ele carrega);
2. **reproduzir o ambiente de sistema** que a dashboard espera e que jogos não
   usam (XAM de sistema, perfis, conteúdo, contexto da dashboard);
3. **integrar menu e jogos**: lançar um título, encerrar e voltar.

Recompilar `dash.xex` não resolve os outros dois.

## 2. Confirmado

### No SDK (ReXGlue v0.10.0)

| Tema | Fato | Referência |
|---|---|---|
| Modelo de execução | todo código precisa ser recompilado e ligado antes; o XEX é carregado só pelos dados | wiki *Kernel State & Objects*; `src/codegen/project_recompiler.cpp` |
| Módulos extras | DLLs de guest viram bibliotecas nativas carregadas por `XexLoadImage` se declaradas em `[[modules]]` com `guest_path` | `src/system/kernel_state.cpp:744-809`; `rexglue init module --guest-path` |
| `XexLoadImage` | implementado (busca módulo carregado ou `LoadUserModule`) | `src/kernel/xboxkrnl/xboxkrnl_modules.cpp:86-122` |
| `XexLoadImageHeaders` | retorna `X_STATUS_NOT_IMPLEMENTED` | `xboxkrnl_modules.cpp:212-215` |
| `XexLoadExecutable`, `XexLoadImageFromMemory` | stubs | `xboxkrnl_modules.cpp:230-231` |
| Lançar título | `XamLoaderLaunchTitle` grava `launch_path` e termina o título; não inicia nada | `src/kernel/xam/xam_info.cpp:263-289` |
| Voltar à dashboard | `XamLoaderLaunchTitle(NULL)` dispara `assert_always("Game requested exit to dashboard...")` | `xam_info.cpp:284` |
| Variantes de lançamento | `XamLoaderLaunchTitleEx`, `...ForReason`, `...OnDvd`: stubs | `src/kernel/xam/xam_misc.cpp:537-539` |
| Contexto da dashboard | `XamGetDashContext(Ex)`, `XamSetDashContext(Ex)`: stubs | `xam_misc.cpp:448-449, 712-713` |
| Dados de lançamento | `XamLoaderSet/GetLaunchData(Size)` implementados | `xam_info.cpp:226-261` |
| Semântica de stub | registra aviso e não define r3 | `include/rex/hook.h:52-56` |
| Cobertura (contagem textual) | xam 177/1735 implementados, xboxkrnl 261/922, xbdm 0/255 | `src/kernel/*/export_table.inc` e `REX_EXPORT*` |
| Formatos de XEX | criptografia normal (AES com chave retail ou devkit), compressão básica e normal (LZX), delta patch | `src/system/xex_module.cpp:35-37, 274, 315, 465-477, 722` |
| `XamIsCurrentTitleDash` | stub (o Xenia implementa comparando com `FFFE07D1`) | `export_table.inc`; Xenia `kernel/xam/xam_info.cc:391-394` |
| `XamBuildResourceLocator`, `XamBuildGamercardResourceLocator`, `XamBuildSharedSystemResourceLocator` | implementados: `file://media:/<container>.xzp#<recurso>` sem módulo, `section://<módulo>,<container>#<recurso>` com módulo | `src/kernel/xam/xam_info.cpp:104-121` |
| `XamXStudioRequest`, `XamGetLiveHiveValueW` | stubs | `export_table.inc` |
| Mensagens de app XAM marcadas como de dashboard no Xenia (10: `0x00200002`, `0x0002B004`, `0x8000000D`, `0x0005008C`, `0x00050094`, `0x00058003`, `0x0007002B`, `0x0007002F`, `0x00070044`, `0x00070053`) | **nenhuma** tratada | `src/kernel/xam/apps/*.cpp` (busca por cada id) |
| Mensagem sem tratador num app existente | registra `Unimplemented <APP> message app=…, msg=…` e retorna `X_E_FAIL` | `xmp_app.cpp:471-474` e equivalentes |
| Mensagem para app inexistente (ex.: o app "messenger" do Xenia) | retorna `X_E_NOTFOUND` **sem log** | `src/system/xam/app_manager.cpp:33-34` |

### No Xenia Canary (referência)

| Tema | Fato | Referência (Xenia Canary) |
|---|---|---|
| Title id da dashboard | `kDashboardID = 0xFFFE07D1` | `src/xenia/kernel/title_id_utils.h:21` |
| Boot de título de sistema | cria `\SystemRoot` apontando para o diretório do executável e carrega **o `xam.xex` real** (ou `$flash_xam.xex`) como módulo de usuário | `src/xenia/emulator.cc:620-641` |
| Endereço fixo | página `0x8E038634` "referenced by dashboards. Data is currently unknown" é reservada | `src/xenia/kernel/kernel_state.h:393-395`, `kernel_state.cc:74` |
| Contexto | `XamSetDashContext` grava o valor e envia `kXNotificationSystemDashContextChanged` | `src/xenia/kernel/xam/xam_info.cc:396-410` |
| Notificação de boot | a dashboard recebe `kXNotificationSystemStorageDevicesChanged` no primeiro listener | `src/xenia/kernel/kernel_state.cc:1052` |
| Diferenças por versão | mensagens XAM específicas para blades 1888–2858, 4532–5787, NXE/Kinect e 7xxx+ | `xam/apps/xlivebase_app.cc`, `xmp_app.cc`, `content_manager.cc` |
| Perfis | a GPD da dashboard (`FFFE07D1`) guarda a lista de jogos abertos | `src/xenia/kernel/xam/user_profile.cc:39-52` |

Leitura: no Xenia a dashboard roda com o código real do `xam.xex` carregado
junto, não só com as funções HLE. No ReXGlue isso significaria recompilar
`xam.xex` como módulo e resolver o conflito com os exports HLE do SDK (abaixo).

## 3. Desconhecido

1. **Quais arquivos existem**: nenhuma versão disponível. A lista exata de módulos
   e recursos depende da versão e precisa ser lida dos próprios arquivos.
2. **Se o codegen aceita `dash.xex`**: formato (criptografia/compressão), descoberta
   de funções e quantidade de chamadas não resolvidas.
3. **Imports por ordinal**: `x360n identify` lista bibliotecas e tamanho das tabelas;
   os ordinais ficam na imagem (possivelmente comprimida/criptografada) e ainda
   não são decodificados.
4. **`xam.xex` real vs HLE**: os imports de `dash.xex` para `xam.xex` são ligados
   estaticamente aos `REX_EXPORT` do SDK. Se o `xam.xex` real for recompilado
   como módulo, não está claro qual implementação atende cada import, nem se o
   `xam.xex` aceita o `xboxkrnl` HLE.
5. **Interface**: a dashboard usa XUI e recursos empacotados (`.xzp`); passa pela
   emulação Xenos (`rexgpu-xenos`). Nada testado.
6. **Perfis e armazenamento**: contêineres STFS, conteúdo instalado, GPD.
7. **Lançamento e retorno**: ver `docs/architecture.md` §4; falta o canal
   título→supervisor e o tratamento de `XamLoaderLaunchTitle(NULL)`.
8. **Endereço `0x8E038634`**: uso desconhecido também no Xenia.

## 4. Como o projeto receberá os arquivos

Somente arquivos que o usuário tem autorização para usar (por exemplo, extraídos
do próprio console). O projeto não baixa nem distribui esses arquivos.

```
.local/inputs/dashboard/<versão>/   (ignorado pelo git)
  dash.xex
  xam.xex            (ou a forma em que estiver no sistema de arquivos de flash)
  demais módulos e recursos que dash.xex referenciar
```

Cada arquivo será registrado por SHA-256 com `x360n identify --json`. Nenhum
script encontrado nesses dados é executado; `x360n run` recusa executáveis em
`.local/inputs`.

## 5. Escolha da versão

Sem arquivos, não há escolha. Critérios para quando houver: (1) versão que o
usuário efetivamente possui; (2) menor número de imports não implementados
medido no experimento D1; (3) versões antigas (blades, antes da NXE) não usam
avatares nem Kinect e devem ter menos dependências de sistema — **[hipótese]**
a confirmar com dados.

## 6. Primeiro experimento (D1) — análise sem executar

Entrada: uma versão autorizada da dashboard em `.local/inputs/dashboard/<versão>/`.

1. `x360n identify --json` em cada XEX: SHA-256, flags, title id (esperado
   `FFFE07D1` para `dash.xex`), versão, formato, bibliotecas importadas.
2. `rexglue init --project-name dash_<versão> --xex-path <dash.xex> --project-root .local/projects/dash_<versão>`
   e `rexglue codegen` **sem `--force`**; guardar log e código de saída.
3. Produzir a matriz de imports: cada ordinal usado → implementado / stub /
   ausente no SDK v0.10.0.
4. Listar módulos carregados dinamicamente (`XexLoadImage`) e recursos abertos.

Critério de saída: relatório com números reais. Iniciar a dashboard fica para D2.
Um processo de teste que abre e fecha valida apenas o supervisor e nunca será
apresentado como execução da dashboard.
