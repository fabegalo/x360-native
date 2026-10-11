# Dashboard original: diagnóstico

Estado em 2026-10-11. **A dashboard original não foi declarada funcionando.**
Versão em trabalho: 2.0.17559.0 (Metro), arquivos fornecidos pelo usuário. A
análise do codegen passou (sem `--force`) depois de 30 entradas `[functions]` e
uma tabela de saltos conferidas uma a uma (§0.4); compila e linka (§0.5). Com os
patches locais 0001/0002 do SDK o setup do runtime conclui e a imagem carrega,
mas o código da dashboard **não chega a executar**: nenhuma função recompilada é
registrada, por um defeito do codegen ainda não corrigido (§0.6–0.7).

Referências: ReXGlue v0.10.0 `f5337cdc` (caminhos relativos a
`external/rexglue-sdk`) e Xenia Canary `04f85416e0f8` (consultado só como
referência de comportamento; não é usado como executor).

## 0. Pacote 17559

Arquivos em `.local/inputs/dashboard/17559/` (ignorado pelo git). Nenhum foi
alterado (hashes reconferidos); hashes em `.local/inputs/dashboard/17559.sha256`,
identidade em `.local/inputs/dashboard/17559-*.identify.json`. Nenhum arquivo de
outra versão foi usado — inclusive os de `.local/inputs/dashboard/Dashboards/`
(Blades, NXE, Metro, Kinect, Fonts; 1223 arquivos), que apareceram durante o
trabalho e não foram abertos além dos nomes de diretório.

Ferramentas (todas sobre o carregador e o decodificador do próprio SDK):
`x360n-content dump-image | imports | resources | disasm | xref | callers`,
`scripts/xex-imports.ps1`, `scripts/xex-module-strings.ps1`,
`scripts/xex-locator-strings.ps1`, `scripts/check-function-bounds.ps1`,
`scripts/check-pdata-overlap.ps1`. Saídas em `.local/runs/dash-artifacts/`.

### 0.1 Conteúdo real

| Arquivo | Bytes | SHA-256 | O que é (evidência) |
|---|---|---|---|
| `dash.xex` | 5 971 968 | `3ad8f38d070eaf63cadac0697e3e8802c565cbfd0825a485347c5d718014f927` | XEX2, flag `title`, title id `FFFE07D1` (dashboard), versão e versão base `2.0.17559.0`, PE `dash.exe`, entry `0x92196610`, base `0x92000000`, imagem `0x1028000`; **sem criptografia** (tipo 0), **compressão normal/LZX** (tipo 2); filetime 2019-10-19 04:37:40 UTC (log do carregador) |
| `dashbigger.xex` | 15 880 192 | `fc4ad90638a8fff7759734dee364a48964e164d567699ca7e7aca2dc06ebc8c2` | mesmos campos de cabeçalho, imports e recursos, mas **compressão básica** (tipo 1). A imagem carregada pelo SDK é **byte a byte igual** à do `dash.xex` (SHA-256 `d98e91e2acaf69e2990f4f4f76f9315483ad04ec8b06e5241a004bfa6f99e888`). Mesmo programa, outra compressão. Qual dos dois o console usa: não verificado |
| `shrdres.xzp` | 223 911 | `179c5be167402297c6e137c08fefe3146f6f497964dc2bda94812596fc9aefe0` | pacote XUI (magic `XUIZ`, versão 3), 118 nomes (112 `.png`, 6 `.xma`). Alcançado pelo esquema `sharedres://` (§0.3) |

**Recursos embutidos no `dash.xex`** (`x360n-content resources`, cabeçalho
`XEX_HEADER_RESOURCE_INFO`; é contra eles que `section://` e `XexGetModuleSection`
resolvem, `src/system/user_module.cpp:258-277`): **36 seções**, idênticas no
`dashbigger.xex`. Uma `XDBF` (`FFFE07D1`, 0x407A bytes) e 35 pacotes `XUIZ`:
`luaxbox`, `friendsc`, `mediasit`, `socxzp`, `soclua`, `controlp`, `thermal`,
`oobe`, `gamer`, `signinpr`, `slots`, `parental`, `epix`, `iptv`, `download`,
`neon`, `signupbo`, `dashuisk`, `network`, `videos`, `pictures`, `music`,
`memory`, `gamercar`, `dvd`, `dashcomm`, `dashmain`, `consoles`, `SharedUI`,
`contui`, `contapp`, `hubui`, `dashlua`, `hubapp`, `arcade` (endereços, tamanhos e
SHA-256 em `.local/runs/dash-artifacts/dash-resources.csv`).

### 0.2 Imports e as conferências antes da execução

Decodificados pelo carregador do SDK (`x360n-content imports`). O cabeçalho tem
três registros — `xam.xex` 2.0.17559.0 duas vezes (623 e 421 entradas) e
`xboxkrnl.exe` 2.0.17559.0 (584) — com 1628 registros. O carregador pareia por
ordinal **dentro de cada registro** (`src/system/xex_module.cpp:1076-1135`);
juntando os dois registros de `xam` por ordinal são **822 imports**.

| Biblioteca | Implementado | Hook direto | Stub | Shim só em fonte não compilada | Variável com valor | Variável sem valor | Ausente no SDK | Total |
|---|---|---|---|---|---|---|---|---|
| `xam` | 125 | 0 | 391 | 0 | 0 | 1 (`0x3DB`, ver abaixo) | 5 | 522 |
| `xboxkrnl` | 184 | 10 | 82 | 8 (`XUsbcam*`, §0.5) | 15 | 1 | 0 | 300 |

A classificação conta só as fontes que o runtime compila (`src/kernel/CMakeLists.txt`);
a versão anterior desta tabela contava todas e dava os 8 `XUsbcam*` como
implementados/stubs — o linker mostrou o erro (§0.5).

**Validação contra o codegen**: os 800 imports de função com nome no SDK são
exatamente os 800 `__imp__*` declarados no código gerado; nenhum nome gerado
fica fora da lista; 794 são chamados diretamente (os 6 restantes:
`IoInvalidDeviceRequest`, `NtQueryDirectoryFile`, `NtReadFileScatter`,
`__C_specific_handler`, `StfsCreateDevice`, `StfsControlDevice`). O codegen
registrou os mesmos 5 `Cannot resolve ordinal` e as mesmas 2 variáveis sem valor.
822 = 800 + 5 ausentes + `0x3DB` + 16 variáveis.

Conferências pedidas (fato = desmontagem ou código citado):

| Item | O que é | Uso no `dash.xex` | Situação |
|---|---|---|---|
| `xam 0x8E2` `XamNuiGetSupportString` | **não é divergência**: um registro de `xam` tem só o slot de valor, o outro só o thunk; somados é função, como no SDK. O relatório anterior estava errado por classificar por registro | 1 chamada (`bl` em `0x92280BC0`, função `0x92280938`) | stub no SDK (não define r3) |
| `xam 0x3DB` `g_XuiAutomation` | divergência real: o XEX tem slot e thunk (função); SDK e Xenia (`xam_table.inc`) declaram **variável**. O codegen o ignora como variável; o carregador grava `0xD3DBBEEF` no slot | **nenhuma**: 0 chamadas ao thunk `0x9293D11C`, 0 leituras do slot `0x92000550`, 0 palavras de dados com esses endereços | sem efeito observável; nada alterado |
| `xam 0x546–0x549, 0x54B` | ausentes na tabela do SDK. No Xenia: `XamSetKerbTimeouts`, `XamGetKerbTimeouts`, `XamSetMacsTimeouts`, `XamGetMacsTimeouts`, `XamProfileHasWindowsLiveCredentials` (nomes **segundo o Xenia**; autenticação do Xbox Live, fora do escopo) | `0x546–0x549` chamados por 3 funções (`0x927C9AA8`, `0x927C9BC0`, `0x927C9CB0`) referenciadas por uma tabela em `.data` (`0x9295B808`); `0x54B` por um wrapper (`b` em `0x928B1E7C`) | o codegen os declara como `sub_9293C88C`, `sub_9293C89C`, `sub_9293C8AC`, `sub_9293C8BC`, `sub_9293CE6C` e os registra, **sem definição** (§0.5) |
| `xboxkrnl 0x157` `XboxKrnlBaseVersion` | variável sem `SetVariableMapping` no SDK; o Xenia também não a define (`xboxkrnl_module.cc` só mapeia `XboxKrnlVersion`) | lida em `0x92280A80` como ponteiro para 4 `uint16` (`lhz` em +0..+6), ao lado de `XboxKrnlVersion` (`0x92280A84`), na função `0x92280938` (a mesma do `0x8E2`), alcançada por `b` de `0x92270FDC` | o slot recebe `0xD157BEEF`; se a função rodar, lê de um endereço inválido. **Não preenchido** (não há valor demonstrado) |

Outros achados que afetam a execução:

- `XdfLoadXexFromCache` (`xam 0x655`) é **stub** e o `dash.xex` o usa para carregar
  `dashnui.xex` (§0.3). Um stub não define r3: no carregamento da tabela A
  (`0x9218B4DC`) r3 continua com o 1º argumento (`0x92003D60`, negativo como
  `int32`) e o código toma o caminho de erro; na tabela B o handle local continua 0
  e o código pula a resolução. Comportamento **decorrente do stub**, não real.
- Com versão de sistema acima de um limite, `0x922478D0` e `0x9288BA28` fazem
  `XexGetModuleHandle("xam.xex")` + `XexGetProcedureAddress(h, 0x50 / 0x24)` e
  chamam o ponteiro; o SDK atende isso criando um thunk para o export HLE
  (`src/system/kernel_module.cpp:39-78`). Os dois exports
  (`NetDll_XNetStartupEx`, `NetDll_WSAStartupEx`) são stubs.

### 0.3 Dependências: classificação

Fontes: recursos embutidos (§0.1), localizadores na imagem
(`scripts/xex-locator-strings.ps1`: 747 textos `<esquema>://`), código que usa
cada nome (`x360n-content xref/callers/disasm`). "Necessário" só com evidência de
uso no caminho de inicialização — nenhuma ainda (a execução não chegou lá).

**Esquemas de recurso.** `0x92181A80` traduz os esquemas próprios da dashboard:
`common://`, `controlpack://` e `epix://` viram
`section://<handle>,<8 primeiras letras do contêiner>#…` por `0x92187BE0`
(formato `section://%X,%s%s%s`, handle do próprio `dash.xex` quando 0) →
seções `dashcomm`, `controlp`, `epix`. Há também `section://%X,dashuisk#skin.xur`,
`section://%X,dvd#…`, `section://%X,luaxbox`.

| Dependência | Classe | Evidência | Presente? |
|---|---|---|---|
| 35 pacotes XUIZ (`dashmain`, `dashcomm`, `dashuisk`, `consoles`, `gamercar`, …) | **recurso interno** | seções do próprio XEX; os nomes `.xzp` das strings correspondem a elas pelas 8 primeiras letras em minúsculas — `0x92187C24` converte cada caractere com `0x921996E0` (A–Z → a–z) — (22 de 26 diretamente: `consolesettings→consoles`, `dashcommon→dashcomm`, `dashuiskin→dashuisk`, …) | sim |
| `BuiltIn.ContentApp`, `BuiltIn.Hub`, `BuiltIn.Social.Lua.Offline` (`.xzp`) | provável recurso interno | textos adjacentes a `contapp`, `hub*`, `soclua` (`channel://BuiltIn.Social.Lua.Offline.xzp` está dentro do recurso `epix`) — **hipótese** | — |
| `shrdres.xzp` | **arquivo externo** | `sharedres://` → `XamBuildSharedSystemResourceLocator` (`bl` em `0x92181B10`) → no SDK `file://media:/shrdres.xzp#…` | sim (pacote), mas o SDK **não monta `media:`** |
| `SegoeXbox-Light.xtt` | arquivo externo | `file://media:/SegoeXbox-Light.xtt`, usado em `0x922655D8` | não; necessidade não demonstrada |
| `kiosk.xzp` | arquivo externo | `kiosk://` → `file://KioskContent:/kiosk.xzp#…` (`0x921910D0`); modo quiosque | não |
| `L.<idioma>.<…>.xzp` | arquivo externo | `langpack://` → `XamPackageManagerGetFilePathW` (stub) | não |
| `Nielsen.xma`, `Sleepies:/…bin`, `VideoContent:\…`, `WallPaper%d`, `devkit:\dash.xgs` | arquivo externo | localizadores `file://` com dispositivos próprios | não; uso no código não seguido |
| `gamercrd.xzp` | arquivo externo segundo o SDK | `XamBuildGamercardResourceLocator` (`bl` em `0x9234182C`) → `file://media:/gamercrd.xzp#…`; o próprio `dash.xex` tem a seção `gamercar` (relação não verificada) | não |
| `dashnui.xex` | **módulo dinâmico** | `XdfLoadXexFromCache("dashnui.xex")` em `0x9218B4DC` e `0x927B130C`; resolve pelo menos 31 + 35 funções `Nui*`/`Speech*` por nome (§0.4; dois slots da tabela A são escritos por outro padrão, não decodificado) | não; Kinect/fala (fora do escopo) |
| `BiometricSetup.xex`, `xna_titlelauncher.xex`, `Title.NewLiveSignup.xex` | módulo dinâmico (lançado) | `XdfLaunchNewImageFromCache` (stub) | não |
| `Guide.Survey.xex` | módulo dinâmico | `XamAppLoad` (stub) | não |
| `Xesledd.xex` | módulo dinâmico (lançado) | `XamLoaderLaunchTitleEx` (stub) via `0x921897D8` | não |
| `xam.xex` | módulo do sistema (HLE no SDK) | `XexGetModuleHandle("xam.xex")` + `XexGetProcedureAddress` por ordinal (§0.2) | o SDK fornece o HLE; o real não está no pacote |
| `xboxkrnl.exe` | módulo do sistema (HLE) | `XexLoadImage("xboxkrnl.exe")` via `0x92192D18` | HLE |
| `xbdm.xex` | módulo de devkit | `XexGetModuleHandle("xbdm.xex")` (`0x929051EC`) | não (devkit) |
| `\Device\Harddisk0\Partition1\DashUIAutomation.xex` | módulo dinâmico | `XexLoadImage` via `0x92192CD0` (`0x9225F548`) | não |
| `dash.natalpregame.xex`, `dash.privacyui.xex`, `dash.search.xex`, `TV2Host.xex`, `mcx:\xboxmcx.xex`, `dash.ClosedCaptionDll.xex`, `default.xex` | **desconhecida** | usados por funções locais ainda não seguidas até um import | não |
| `Guide.MP.*.xex`, `deviceselector.xex`, `vk.xex`, `\loadall.dll` | desconhecida | sem referência no código (alguns textos estão dentro de recursos embutidos) | não |

**Nenhum arquivo foi pedido**: nenhum dos ausentes tem uso demonstrado na
inicialização. Um pedido só será feito com nome exato, versão (2.0.17559.0) e a
evidência de abertura (§0.6).

### 0.4 Análise e geração de código

Configuração: `titles/dash_17559/analysis/dash_17559.toml` (versionada; cada
entrada com início, limites, classificação e evidência em comentário; só a
entrada `{}`, sem tamanho nem nome).

| Run | Entradas | Resultado | Novos alvos |
|---|---|---|---|
| `codegen-2` | 0 | falha: 28 `UnresolvedCall`, 23 endereços | — |
| `codegen-3` | 23 | falha: 3 | `0x922E3D18`, `0x927B1250`, `0x927B1290` (atrás dos anteriores) |
| `codegen-4` | 26 | falha: 2 | `0x922E3CB8`, `0x927B1260` |
| `codegen-5` | 30 | **ok** (saída 0, 257 arquivos, 133 MB) | — ; mas `check-pdata-overlap.ps1`: 1 sobreposição |
| `codegen-6` | 30 + 1 tabela | **ok**, 0 sobreposições em 28 350 funções `.pdata`, 37 856 funções geradas | — |

Por que cada rodada achou mais: os alvos aparecem só quando a função que os
chama passa a existir. Para não depender só disso, `x360n-content callers`
classificou todos os 65 stubs das duas tabelas (33 + 32): **todo stub com chamador `bl`
nunca foi reportado; todo stub reportado só tem chamadores `b`** — a análise
promove destinos de `bl` a funções, não destinos de `b` sem `.pdata`. Na cadeia
de manipuladores `0x922E3BC0…0x922E3E00` todos são alcançados só por `b` e estão
em `.rdata` (`0x9202BE00–0x9202BEDC`); `0x922E3C38` e `0x922E3BC0` foram
incluídos antes de o codegen reportá-los, com a mesma evidência, e isso está
anotado.

Classificação das 30 entradas:

| Classe | Quantas | Endereços |
|---|---|---|
| thunk de ajuste `addi r3,r3,8; b` | 6 | `0x9237E668/670/678/710/718/720` |
| thunk de base virtual / argumento fixo | 2 | `0x926745B0`, `0x927CFCF0` |
| thunk com destino dinâmico (`lis/lwz/mtctr/bctr`) | 12 | `0x9218B410`; `0x927B11F0`, `…1210`, `…1220`, `…1230`, `…1240`, `…1250`, `…1260`, `…1270`, `…1280`, `…1290`, `…12A0` |
| função | 10 | `0x922E3BC0`, `0x922E3C38`, `0x922E3CB8`, `0x922E3D18`, `0x922E3D88`, `0x9234F658`, `0x92351430`, `0x923766B0`, `0x927AFE08`, `0x92837DE8` |
| trecho de outra função | 0 | — |

**Limites conferidos no código gerado** (`scripts/check-function-bounds.ps1`):
30/30 iguais aos documentados. A única diferença inicial era minha: em
`0x923766B0` incluí um `blr` inalcançável depois de um `bctr`.

**Destinos dinâmicos** (os 12 thunks): o endereço de entrada é estático; o
destino é lido de duas tabelas em `.data`, zeradas na imagem e escritas só por
`0x9218B4A8` (tabela A, `0x92992C88–0x92992D0C`) e `0x927B12B0` (tabela B,
`0x92A77A28–0x92A77AB0`, zerada de novo por `0x927B16A8`). Ambas fazem
`XdfLoadXexFromCache("dashnui.xex", 8, 0, &handle)` e resolvem cada slot com
`XexGetProcedureAddress` por **nome** (`NuiIdentityEnroll`,
`SpeechOnlineRecognize`, …; mapa slot→nome em
`.local/runs/dash-artifacts/`). Nenhum destino foi fixado. Com o SDK atual o
carregamento não acontece (stub, §0.2), as tabelas ficam em 0, e uma chamada a
um desses thunks chegaria ao despachante com destino 0 —
`Call to invalid or unregistered function at guest address 0x00000000`. Isso é o
que o diagnóstico de execução procura (§0.6).

**Tabela de saltos** (`codegen-5`): `sub_922B4890` saiu com 512 casos e um rótulo
dentro da função seguinte. A tabela **tem** verificação de limite, mas sobre `r4`
antes da cópia para `r25` (`cmplwi cr6,r4,22`); são 23 bytes em `0x920273D0`,
seguidos de outro dado (texto UTF-16). Declarada em `[[switch_tables]]`; o
switch gerado passou a ter 23 casos.

**Desvio para preenchimento** (fase Write, não bloqueia a validação):
`Unresolved conditional branch to 0x927E539C from 0x927E5358`. É um `beq` para a
palavra `0x00000000` logo após o fim `.pdata` da função `0x927E5320` (não é
instrução válida), tomado só se `*(r31+4) == 0`. O codegen emite
`if (cr6.eq) REX_FATAL("Unresolved branch …")`. **Não** foi transformado em
função: não há código lá.

### 0.5 Compilação e linkedição (SDK `f5337cdc`, `RelWithDebInfo`)

`scripts/build-title.ps1 -Title dash_17559` (logs em
`.local/build/titles/dash_17559/x360n-build-*.log`):

| Etapa | Resultado | Evidência |
|---|---|---|
| 1 configure (SDK do submódulo, na árvore do título) | ok, 400 s | backport `b5e0cf8` aplicado |
| 2 `dash_17559_codegen` (rexglue da árvore do título) | ok, 111 s | `1 module(s) up to date`: usou a saída do `codegen-6`, não regenerou |
| 3 reconfigure | ok, 7 s | — |
| 4 build | compilação **ok**; **linkedição falhou** (106 s) | 13 × `lld-link: error: undefined symbol`: `__imp__XUsbcamSetCaptureMode`, `…GetConfig`, `…SetConfig`, `…GetState`, `…ReadFrame`, `…SetView`, `…Create`, `…Destroy` (o lld os mostra como `__declspec(dllimport) _XUsbcam…`) e `sub_9293C88C`, `sub_9293C89C`, `sub_9293C8AC`, `sub_9293C8BC`, `sub_9293CE6C` |
| 5 build após a correção | **ok**, 35 s | `dash_17559.exe` 47 486 976 bytes, SHA-256 `0191989c6a46f4e6363f2c3b9838f0ca2cc0368ccf357833a1ae956b63c80d8e` |

Causas:

1. `XUsbcam*`: os shims existem em `src/kernel/xboxkrnl/xboxkrnl_usbcam.cpp`, mas o
   arquivo **não é compilado**: `src/kernel/CMakeLists.txt:53` traz
   `# xboxkrnl/xboxkrnl_usbcam.cpp  # TODO: lol eventually.` (o mesmo vale para
   `cert_monitor.cpp` e `debug_monitor.cpp`; o `development` upstream está igual,
   não há commit para portar). A minha classificação anterior lia todas as fontes
   da pasta e os dava como "implementado/stub" — **errado**. O script agora lê só as
   fontes listadas no build e bate exatamente com o linker: 8 "sem shim" + 5
   "ausentes" = os 13 símbolos.
2. Ordinais ausentes: o codegen registra e chama os thunks sob nomes provisórios
   e não emite definição.

Correção, no título e não no SDK: `titles/dash_17559/src/unresolved_imports.cpp`
define os 13 símbolos como **armadilhas** — `REX_FATAL` com o import, o motivo e o
LR do guest. Nada é emulado, nenhum valor é devolvido, nenhum retorno de sucesso.
O app do título também carrega `game:\dash.xex` em vez do padrão
`game:\default.xex` (`OnLoadXexImage`, `src/ui/rex_app.cpp:262`).

### 0.6 Primeira execução: bloqueio concreto

```powershell
x360n run .local\build\titles\dash_17559\dash_17559.exe --label dash17559-run-1 --timeout 60 --rex-log -- `
  --gpu_plugin=xenos --game_data_root=<raiz>\.local\inputs\dashboard\17559 `
  --user_data_root=<raiz>\.local\user\dash_17559 --log_max_file_size_mb=512
```

Resultado (run `20261011T220402Z-dash17559-run-1`, exe `0191989c…`): o processo
terminou em **5,7 s** com `STATUS_ACCESS_VIOLATION`, sem entrada enviada à janela.
O log do runtime termina em:

```text
[error] [sys]  BaseHeap::AllocFixed invalid base alignment: base=93028000 page_size=00010000 requested_alignment=00010000 heap=80000000-8FFFFFFF
[error] [sys]  Failed to allocate function table at 93028000
[error] [core] Failed to initialize guest memory function table
[error] [sys]  Failed to initialize function table
[error] [core] Runtime setup failed: C0000001
```

Causa (fato, pelo código): `Memory::InitializeFunctionTable`
(`src/system/xmemory.cpp:787-806`) põe a tabela de funções do código
recompilado logo depois da imagem (`image_base + image_size` = `0x92000000 +
0x1028000` = `0x93028000`) e a pede **sempre** ao heap `v80000000`
(`xmemory.cpp:798`; heap `0x80000000–0x8FFFFFFF`, páginas de 64 KB,
`xmemory.cpp:189`). Jogos ficam em `0x82000000` e cabem; a dashboard fica em
`0x92000000`, dentro do heap `v90000000` (`0x90000000–0x9FFFFFFF`, páginas de
4 KB, `xmemory.cpp:191`). O `development` upstream tem o mesmo código.

Consequências medidas: o setup do runtime (`src/ui/rex_app.cpp:237-240`) acontece
**antes** do carregamento do XEX (`rex_app.cpp:262` em diante), então **nenhuma
instrução da dashboard executou**: 0 aberturas de arquivo, 0 carregamentos de
módulo, 0 stubs, nenhum aviso de variável de import (o carregador ainda não tinha
rodado). A janela capturada estava preta.

**A violação de acesso é um segundo defeito**, separado da falha de alocação. Pilha
capturada com `x360n run --crash-stack` (run `20261011T222321Z-dash17559-run-1b-crashstack`,
mesmo executável, SDK ainda sem patch):

```text
exception 0xC0000005 at 0x00007FF85EFA3B3E, read 0x00007FF88BA76088
#0 D3D12Core+0x53B3E
#1 rexruntimerd!…::D3D12ImmediateTexture::~D3D12ImmediateTexture+0x8D (src/ui/d3d12/d3d12_immediate_drawer.cpp:45)
#3 rexruntimerd!rex::ui::ImGuiDrawer::SetImmediateDrawer+0x5A (src/ui/imgui_drawer.cpp:369)
#4 dash_17559!rex::ReXApp::OnDestroy+0x19D (src/ui/rex_app.cpp:580)
#5 dash_17559!wWinMain+0x69B (src/ui/windowed_app_main_sdl.cpp:124)
```

Causa (fato, pelo código): ao falhar, `Runtime::Setup` chamava `Shutdown()`
(`src/system/runtime.cpp:212`), que destrói o sistema gráfico e o device D3D12
(`runtime.cpp:268-270`). Esse sistema tinha sido criado pelo `ReXApp` e movido
para o runtime (`rex_app.cpp:237`); o drawer, a textura de fonte do ImGui e o
presenter da janela feitos com ele continuavam com o app. O `OnDestroy` os
liberava depois — leitura em memória já liberada do `D3D12Core`.

Correções, autorizadas em 2026-10-11 como patches locais sobre `f5337cdc`
(`patches/rexglue-sdk/README.md`), cada uma com um teste que falha sem ela:
**0001** aloca a tabela no heap XEX que contém o intervalo inteiro, mantendo o
endereço e a indexação usados pelo código gerado; **0002** faz um `Setup` que
falhou deixar a desmontagem para `~Runtime()`, na mesma ordem de uma saída normal.
Resultado da nova execução em §0.7.

Diagnóstico pronto para a próxima execução: o relatório do supervisor agora
inclui `failed_opens` (caminho → status), `unregistered_calls` (destino),
`unimplemented_variables`, `error_lines` e `first_errors`; o log pode ir para
`--log_level=trace --log_noisy=true` para registrar também as aberturas bem-
sucedidas (`[NtCreateFile] path=…`). O SDK não registra o chamador de uma
abertura; as armadilhas de §0.5 registram o LR.

### 0.7 Execução com os patches 0001 e 0002

Mesmo comando de §0.6, runs `20261011T225551Z-dash17559-run-2` e (binários finais)
`20261011T230202Z-dash17559-run-3`; exe `f06a3c54…ed4c5300`. O relatório do `run`
registra as DLLs ao lado do executável: `rexruntimerd.dll` `eeb72b10…99be01db`,
idêntica à do SDK corrigido em `external/rexglue-sdk/out`, e o log contém a linha que
só o código do patch 0001 escreve.

| Etapa | Resultado | Evidência |
|---|---|---|
| Setup do runtime | **concluído** | `Function table at 93028000-93FC9917 in XEX heap 90000000 (1000-byte pages) for code 9217E000-9293EC8C`; `Function table initialized for module: code=9217E000-9293EC8C, image=92000000-93028000` |
| Imagem da dashboard carregada | **sim** | o carregador rodou: `Variable import xam:0x3db (g_XuiAutomation) not implemented`, `… xboxkrnl:0x157 (XboxKrnlBaseVersion) …`, `Initializing shader storage for title FFFE07D1`; com `--log_level=debug`, `Patched variable import xboxkrnl:0x193 (XexExecutableModuleHandle)` e `KernelState: Preparing module launch` |
| Entrada no código recompilado | **não aconteceu** | `XThread::Execute - No function registered at 92196610` (o entry point) |
| Primeira falha real | ver abaixo (A) | — |
| Encerramento | **falha** (B): `STATUS_HEAP_CORRUPTION` aos 2,0 s; sob o depurador, violação de acesso | ver abaixo |
| Renderização / navegação | não observadas | o código da dashboard não executou |

**(A) Nenhuma função recompilada registrada.** Com `--log_level=debug`:
`Registered 0 recompiled functions (0 duplicates, 0 rejected)`. A primeira linha de
`PPCFuncMappings` gerado é `{ 0x0, __imp__XamNuiGetSupportString }`, e o runtime
para de registrar na primeira entrada com `guest == 0` (o terminador da lista).
Origem: o `xam 0x8E2` (`XamNuiGetSupportString`) tem o slot de valor num registro
de import e o thunk no outro (§0.2). O registro sem thunk vira um símbolo de import
com `address = thunk_address = 0` (`XexModule::PopulateBinaryData`,
`src/system/xex_module.cpp:1163-1171`, sem teste de zero), e como o SDK declara
`0x8E2` função, o codegen o registra no endereço 0. Defeito do SDK (codegen) **não
corrigido**: fica fora do que foi autorizado. Correção proposta (patch 0003, com
teste): não criar símbolo de import para registro sem thunk; depois, codegen e
build da dashboard de novo.

**(B) Encerramento normal usa a janela destruída.** Pilha (`--crash-stack`, run
`20261011T225728Z-dash17559-run-2b-debug`):
`WindowedAppContext::CallInUIThreadSynchronous` ← `~MnkInputDriver`
(`src/input/mnk/mnk_input_driver.cpp:155-157`, `DetachFromWindow`) ←
`InputSystem::Shutdown` ← `Runtime::Shutdown` ← `~Runtime` ← `ReXApp::OnDestroy`
(`src/ui/rex_app.cpp:606`). O `OnDestroy` faz `window_.reset()` (`:605`) antes de
`runtime_.reset()`, e o driver de teclado/mouse, ligado à janela por `AttachWindow`
depois de um setup bem-sucedido, a usa ao ser destruído. Acontece quando o app sai
sozinho (aqui, porque a thread principal do guest terminou); os runs do Castle
Crashers sempre terminaram por timeout. **Não corrigido** (fora da autorização).
Que o `STATUS_HEAP_CORRUPTION` sem depurador seja o mesmo uso após liberação é
**hipótese** (mesmo ponto, logo após `Execution complete`).

**Caminho de erro do setup (item autorizado), conferido de ponta a ponta:** com
só o 0002 aplicado (0001 revertido para reproduzir a falha), o mesmo setup falha
com os mesmos 5 erros e o processo termina com **saída 1, sem exceção não tratada**
(`--crash-stack`, run `20261011T230042Z-dash17559-errorpath-0002`). Antes do 0002,
violação de acesso no `D3D12Core`. O driver de entrada não participa desse caminho:
ele só recebe a janela depois de um setup bem-sucedido (`rex_app.cpp:243-245`).

Nenhuma abertura de arquivo nem carregamento de módulo pelo guest: o código da
dashboard não executou (`failed_opens` vazio). As 13 armadilhas continuam no título.

### 0.8 Ainda desconhecido

- o que vem depois do registro das funções (§0.7 A); nenhuma instrução da
  dashboard executou ainda;
- tudo o que depende de executar código da dashboard: se o HLE de `xam` basta,
  quais arquivos e módulos ela abre de fato, se chama os 12 thunks dinâmicos, os
  imports-armadilha ou a função que lê `XboxKrnlBaseVersion`;
- o papel das dependências marcadas "desconhecida" em §0.3;
- qual dos dois XEX o console carrega.

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

1. **Quais arquivos são necessários**: para a 17559 o pacote tem 3 arquivos
   (§0.1); o que mais é aberto em execução só se sabe analisando o código ou
   executando (§0.3).
2. *(resolvido para a 17559)* Descoberta de funções: passa com 30 entradas e uma
   tabela de saltos conferidas (§0.4).
3. *(resolvido)* Imports por ordinal: decodificados (§0.2).
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

Versão em uso: **2.0.17559.0**, a que o usuário forneceu. Critérios para uma
eventual segunda versão: (1) versão que o usuário efetivamente possui; (2) menor número de imports não implementados
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
Resultado para a 17559: passos 1–4 feitos (§0.2–0.4; o projeto ficou em
`titles/dash_17559`, não em `.local/projects`). D2 (iniciar) parou no setup do
runtime, antes de executar código da dashboard (§0.6).
Um processo de teste que abre e fecha valida apenas o supervisor e nunca será
apresentado como execução da dashboard.
