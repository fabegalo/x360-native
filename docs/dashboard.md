# Dashboard original: diagnóstico

Estado em 2026-10-12. **Intro original e menu Metro; interface ainda parcial.**
A 17559 usa agora SegoeXbox-Light.xtt original. My Games recebe a URI expandida,
mas a biblioteca ainda aparece sem conteúdo; algumas configurações causam crash.
§0.13 registra o incremento atual; as seções
anteriores preservam a evolução do diagnóstico. A região zerada de §0.10
continua sendo uma hipótese experimental, com conteúdo real desconhecido.

Referências: ReXGlue v0.10.0 `f5337cdc` (caminhos relativos a
`external/rexglue-sdk`) e Xenia Canary `04f85416e0f8` (consultado só como
referência de comportamento; não é usado como executor).

## 0. Pacote 17559

Arquivos em `.local/inputs/dashboard/17559/` (ignorado pelo git). Nenhum foi
alterado (hashes reconferidos); hashes em `.local/inputs/dashboard/metadata/17559/17559.sha256`,
identidade em `.local/inputs/dashboard/metadata/17559/17559-*.identify.json`. Nenhum arquivo de
outra versão foi usado no alvo ativo. A coleção fornecida posteriormente está em
`.local/inputs/dashboard/versions/<versão>/<variante>/`, e as fontes em
`dashboard/resources/fonts/`.

Na organização de 2026-10-12, os 311 conjuntos da coleção e o alvo ativo foram
comparados por caminhos relativos, tamanhos e SHA-256: 242 conjuntos preservados,
70 pastas redundantes removidas (205 arquivos, 364 861 233 bytes). Cada arquivo
removido tem uma cópia verificada no destino indicado em
`dashboard/metadata/organization/removed-duplicates.csv`; os nomes antigos e
variantes permanecem em `catalog.csv`. Conjuntos com recursos diferentes não
foram tratados como duplicados. O complemento `17559 Full` está separado em
`versions/17559/metro_v2_retail_full_readme/`, sem misturá-lo ao alvo ativo.

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
`xboxkrnl.exe` 2.0.17559.0 (584) — com 1628 registros. No SDK fixado o carregador
pareia por ordinal **dentro de cada registro** (`src/system/xex_module.cpp:1076-1135`);
juntando os dois registros de `xam` por ordinal são **822 imports**. O patch 0003
passou a juntar as metades de um mesmo import entre registros da mesma biblioteca
(§0.8).

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
`0x8E2` função, o codegen o registra no endereço 0. Defeito do SDK (carregador e
codegen), **corrigido depois pelo patch 0003** (§0.8).

**(B) Encerramento normal usa a janela destruída.** Pilha (`--crash-stack`, run
`20261011T225728Z-dash17559-run-2b-debug`):
`WindowedAppContext::CallInUIThreadSynchronous` ← `~MnkInputDriver`
(`src/input/mnk/mnk_input_driver.cpp:155-157`, `DetachFromWindow`) ←
`InputSystem::Shutdown` ← `Runtime::Shutdown` ← `~Runtime` ← `ReXApp::OnDestroy`
(`src/ui/rex_app.cpp:606`). O `OnDestroy` faz `window_.reset()` (`:605`) antes de
`runtime_.reset()`, e o driver de teclado/mouse, ligado à janela por `AttachWindow`
depois de um setup bem-sucedido, a usa ao ser destruído. Acontece quando o app sai
sozinho (aqui, porque a thread principal do guest terminou); os runs do Castle
Crashers sempre terminaram por timeout. **Corrigido depois pelo patch 0004** (§0.8).
Que o `STATUS_HEAP_CORRUPTION` sem depurador fosse o mesmo uso após liberação era
hipótese; o app de teste do 0004 reproduz a mesma pilha e termina com `0xC0000005`.

**Caminho de erro do setup (item autorizado), conferido de ponta a ponta:** com
só o 0002 aplicado (0001 revertido para reproduzir a falha), o mesmo setup falha
com os mesmos 5 erros e o processo termina com **saída 1, sem exceção não tratada**
(`--crash-stack`, run `20261011T230042Z-dash17559-errorpath-0002`). Antes do 0002,
violação de acesso no `D3D12Core`. O driver de entrada não participa desse caminho:
ele só recebe a janela depois de um setup bem-sucedido (`rex_app.cpp:243-245`).

Nenhuma abertura de arquivo nem carregamento de módulo pelo guest: o código da
dashboard não executou (`failed_opens` vazio). As 13 armadilhas continuam no título.

### 0.8 Execução com os patches 0003, 0004 e 0005 (2026-10-12)

Mesmo comando de §0.6. Correções feitas nesta etapa, cada uma com a falha que a
motivou; as do SDK estão em `patches/rexglue-sdk/README.md`, com teste que falha sem elas.

| # | Correção | Onde | Motivo (fato) |
|---|---|---|---|
| 1 | patch 0003: o carregador junta slot e thunk de um import entre registros da mesma biblioteca; o codegen não mapeia import no endereço 0 | SDK | §0.7 (A) |
| 2 | patch 0004: o `OnDestroy` desmonta o runtime antes da janela | SDK | §0.7 (B) |
| 3 | `XUsbcamGetState` implementado (estado 0, "não conectado"); as outras 12 armadilhas ficam | título | ver abaixo |
| 4 | 515 inícios de função alcançados só por ponteiro | análise do título | ver abaixo |
| 5 | patch 0005: `KeGetImagePageTableEntry` com bit 0 nas páginas do heap XEX de 4 KB | SDK | ver abaixo |
| 6 | `\SystemRoot` montado, só leitura, na pasta do pacote | título | ver abaixo |

**Código gerado** (`codegen-8`, `rexglue codegen --ignore-stamp`, sem `--force`; o
carimbo do codegen não inclui o próprio `rexglue`, `docs/architecture.md` §7 item 20):
`dash_17559_init.cpp` passa de 38 662 para 38 661 mapeamentos, nenhum com `guest`
0; `{ 0x9293B3CC, __imp__XamNuiGetSupportString }` uma vez; a entrada
`{ 0x92196610, xstart }` existe. No runtime: `Paired import xam:0x8e2 across import
library records (slot 920008DC, thunk 9293B3CC)` e `Registered 38661 recompiled
functions (0 duplicates, 0 rejected)`. Com as entradas do item 4 (`codegen-9`,
`codegen-10`): 39 141 funções, 0 sobreposições com `.pdata`
(`check-pdata-overlap.ps1`), mesma única advertência de §0.4.

| Run | Com | Até onde a dashboard chegou | Parada |
|---|---|---|---|
| `20261012T002758Z-dash17559-run-4` | 1, 2 | entrada executada: 2 mensagens `XLIVEBASE` não implementadas, 7 stubs (`XamGetLanguage`, `XamPackageManagerGetExperienceMode`, …), `XConfig` categoria 7 | armadilha `xboxkrnl 0x21D XUsbcamGetState`, LR `0x9236857C` |
| `…T003040Z-…run-5` | + 3 | a própria dashboard liga `media:` → `\SystemRoot`, registra o callback de interrupção da GPU | `Call to invalid or unregistered function at guest address 0x92394200` |
| `…T004635Z-…run-6` | + 4 (506) | registra um cliente de áudio; o callback de áudio dela roda | não registrada `0x921DC8F8` |
| `…T010713Z-…run-7`, `…T010738Z-…run-7b-crashstack` | + 4 (9) | `NetDll_XHttpStartup`, `XamLoaderRegisterLaunchRequestCallback` (stubs) | `KeBugCheck` chamado pela dashboard (pilha `--crash-stack`) |
| `…T011724Z-…run-8` | + 5 | idioma, fontes (`XamGetLanguageTypeface…`), configurações de perfil, `XConfig`; abre arquivos | `media:\shrdres.xzp` → `device not found`; violação de acesso |
| `…T011850Z-…run-9-crashstack`, `…T012216Z-…run-10` | + 6 | `shrdres.xzp` deixa de falhar; abre `DASHUSER:\ThematicSkin` e `DASHUSER:\VisionEffect` (dispositivo ausente, não fatal até aqui) | `Unhandled guest access violation: read of guest 0x8E038630`, em `sub_92185790` ← `sub_921870A0` ← `xstart` |

Executável final `cec26d64…40bc12`, `rexruntimerd.dll` `de2b6f17…dd9d013d` (idêntica à
do SDK, registrada pelo relatório do `run`). Etapas, separadas:

| Etapa | Resultado |
|---|---|
| Setup do runtime | concluído (tabela de funções em `93028000-93FC9917`) |
| Imagem carregada | sim |
| Funções registradas | 39 141, 0 duplicadas, 0 rejeitadas |
| Entrada no código recompilado | **sim**: `xstart` (`0x92196610`) executa na thread principal do guest |
| Primeira falha real depois da entrada | armadilha `XUsbcamGetState` (run-4), resolvida com implementação (item 3) |
| Parada atual | leitura da página fixa `0x8E038600` (abaixo), ~2 s após o início, ainda na inicialização chamada por `xstart` |
| Renderização | **não observada**: nenhum `VdSwap` no log, nenhum quadro; as capturas da janela não chegaram a ser feitas (o processo termina antes) |
| Navegação | não observada |

**Item 3, `XUsbcamGetState`.** A dashboard o chama na inicialização (run-4). Não há
câmera neste PC nem suporte a câmera no runtime, e a resposta real é "não conectada".
Que 0 significa isso vem do próprio `dash.xex`: `0x92337AB8` faz `presente = (estado
!= 0)` e só então carrega o recurso `ThemeVisionEffect…` (texto UTF-16 em
`0x92032D98`); `0x92368568` leva o estado 2 a mais configuração da câmera. O shim do
SDK que não é compilado e o Xenia também dão "0 = not connected". As outras 7
`XUsbcam*` e os 5 ordinais ausentes continuam armadilhas; nenhuma foi chamada.

**Item 4, funções só por ponteiro.** `0x92394200` é uma folha de 6 instruções depois
do `bctr` que termina `sub_923941E0`, guardada em `.rdata` (`0x9203A468`,
`0x9204326C`), sem `.pdata`. O codegen só acha vtables pelo RTTI e tem o scan geral de
ponteiros desligado (`docs/architecture.md` §7 item 22). Em vez de um endereço por
execução, `scripts/check-pointer-targets.ps1` classifica todos os endereços de código
guardados em dados (`x360n-content xref`, 54 410 palavras) e, numa segunda passada,
os montados no código por `lis` + `addi`/`ori`: 506 + 9 passam em todos os critérios
(alinhado; fora de `.pdata` e de função gerada; palavra anterior 0 ou fim de função;
`blr`/`bctr`/`b` adiante sem cruzar outro início), 395 + 567 não. Ficam em
`titles/dash_17559/analysis/dash_17559_pointer_targets.toml`, separado das entradas
conferidas uma a uma, com o critério e o comando no cabeçalho. Conferi 9 + 9 por
desmontagem (thunks de ajuste `addi r3,r3,-N; b`, thunks de despacho, folhas). Com
elas, 35 funções que o preenchimento de lacunas tinha criado viraram blocos das novas
(casos de `switch` e alvos de desvio condicional); nenhuma é alvo de `bl` nem guardada
em dados.

**Item 5, `KeGetImagePageTableEntry`.** `KeBugCheck(0xF7)` vem de `0x928EF408`, que o
`luaZ_fill` (`0x928EA240`) chama para cada buffer de trecho Lua: o endereço tem de
estar em `0x80000000–0x9FFFFFFF` e o bit 0 de `KeGetImagePageTableEntry` da página
tem de estar ligado, senão bugcheck, salvo um sinalizador do estado global (0 aqui).
Diagnóstico temporário (`…T011201Z-dash17559-diag-luabuf`, removido depois): o buffer
é `0x92FD2204`, página de dados só-leitura da própria imagem (descritor tipo 3), e o
SDK devolvia `0x40002FD2` — o índice da página, bit 0 limpo em toda página par. O
Xenia Canary `331ddf79bf96` liga o bit 0 nesse heap ("dashboards 14xxx and higher"
precisam dele); o patch 0005 faz o mesmo. O significado do bit (página válida ou
só-leitura) não está verificado.

**Item 6, `\SystemRoot`.** O próprio `dash.xex` liga `media:` a `\SystemRoot` e abre
`media:\shrdres.xzp`; no console é a partição de sistema com `dash.xex` e
`shrdres.xzp`. O título monta a pasta do pacote ali, só leitura
(`src/dash_17559_app.h`, `OnPostSetup`). É a primeira dependência de arquivo
**demonstrada**: `shrdres.xzp`, que já estava no pacote.

**Parada atual: página de sistema em endereço fixo.** A dashboard lê, com endereço
absoluto no código (`lis r11,0x8E04`), `0x8E038614` (16 bits; 3 funções), `0x8E038630`
(64 bits, testa `0x40`; `0x92185790` e `0x92186310`) e `0x8E038678` (64 bits, testa
`0x2`; 2 funções). Nada no SDK mapeia essa página, e a leitura falha. O Xenia Canary
aloca a página `0x8E030000` zerada, com o comentário "fixed address referenced by
dashboards. Data is currently unknown" (`kernel_state.h`, `strange_hardcoded_location_
= 0x8E038634`). **Não foi mapeada aqui**: zerá-la seria escolher os valores desses
sinalizadores por suposição. Para continuar é preciso o conteúdo dessa página como o
sistema do console a preenche (qual componente o faz não foi determinado; não é
arquivo do pacote), ou uma decisão explícita de aceitar a página zerada como
suposição registrada, como faz o Xenia.

Riscos já vistos, não corrigidos: 28 stubs diferentes foram chamados, e um stub não
define r3 (por exemplo `XamGetLanguage`); o caminho seguido depois deles pode não ser
o do console. `DASHUSER:` não existe no SDK.

### 0.9 Ainda desconhecido (antes do experimento de §0.10)

- o conteúdo e o dono da página `0x8E038600` (acima) e o que vem depois dela;
- se a dashboard chega à primeira tela: nenhum quadro foi apresentado ainda;
- o papel de `DASHUSER:` (cache/usuário da dashboard) e se a falha dessas aberturas
  importa;
- o efeito dos stubs que não definem r3 no caminho seguido;
- o papel das dependências marcadas "desconhecida" em §0.3, e se ela chama os 12
  thunks dinâmicos, os imports-armadilha ou a função que lê `XboxKrnlBaseVersion`;
- qual dos dois XEX o console carrega.

### 0.10 Região experimental, idioma e espera de conclusão da GPU (2026-10-12)

Continuação autorizada no último chat local do Claude (`63a7c2df-636d-461c-8d81-018cabd8be86`):
aceitar a região zerada como hipótese explícita, seguir a inicialização e corrigir
contratos demonstrados. O pin do SDK permanece `f5337cdc`.

`titles/dash_17559/src/experimental_region.h` reserva e compromete exatamente
64 KiB em `0x8E030000`, RW, antes de iniciar threads guest. A região é zerada uma
única vez por runtime; chamadas posteriores preservam escritas. Qualquer
sobreposição/reserva anterior ou falha de alocação aborta o setup. A hipótese
fica apenas no título 17559, habilitada por padrão e desligável por
`--dash17559_experimental_region=false`. Referência:
[Xenia Canary 2372a54105d9, kernel_state.cc](https://github.com/xenia-canary/xenia-canary/blob/2372a54105d9d598ab5bc7ba94a72ef1bea4b271/src/xenia/kernel/kernel_state.cc).
O conteúdo real continua desconhecido; não há tratamento genérico de access
violation. Log e `report.json` (`diagnostics.compatibility_experiments`) registram
literalmente **“região de compatibilidade experimental; conteúdo real desconhecido”**.

Comparação controlada, mesmo EXE, mesmas DLLs e pastas de dados/usuário, timeout
30 s e `--crash-stack`, mudando somente o sinalizador:

| Run | Resultado |
|---|---|
| `20261012T020213Z-dash17559-region-off` | `0xC0000005`, leitura de `0x8E038630` em `sub_92185790` |
| `20261012T020247Z-dash17559-region-on` | ultrapassa a leitura; timeout sem essa exceção; novas chamadas de inicialização |

O teste `sdkcheck.dashboard experimental region...` verifica limites, proteção,
zeros, preservação de escritas (inclusive bit `0x40` em `0x8E038630`), segunda
instância e sobreposição com memória reservada. O teste do scanner confere a
divulgação em UTF-8 e sua agregação.

O patch SDK **0006** implementa os dois contratos de idioma efetivamente usados:
`XamGetLanguage` lê `user_language`, e `XamGetLanguageLocaleFallbackString` retorna
um ponteiro guest válido para UTF-16 big-endian. O stub deste último devolvia o
ID de idioma residual em r3: com idioma 1, `sub_921CB1E8` tentava ler o endereço 1
(run `20261012T020718Z-dash17559-language-trace`). Testes antes/depois e limites
da implementação estão em `patches/rexglue-sdk/README.md`. Fontes continuam
parciais; não se substituíram outros stubs por sucesso.

**Resultado histórico, antes de §0.11:** o log com trace (`20261012T021146Z-dash17559-fallback`)
mostra criação do render target, resolve e textura 1280×720 em `0x1F6F8000`, no
caminho de apresentação. A janela observada permanece preta. Isso não comprova
uma tela funcional: **menu e navegação não foram alcançados**.

O próximo bloqueio concreto é uma espera de conclusão no caminho de swap:

```text
sub_921A40B0 / sub_921AFF68
  <- sub_921A5AD8 <- sub_92186D90 <- sub_92185790
  <- sub_921870A0 <- xstart
```

Duas amostras de pilha no run `20261012T021300Z-dash17559-hang` e uma no run
`20261012T021956Z-dash17559-fence-sample` mostram a mesma espera. No último, a
leitura externa, sem escrever no processo, encontrou dispositivo `0x40002400`,
ponteiro do contador (`+11024`) `0xFFCA3000`, contador emitido (`+11036`) `0x15`,
alvo da espera (`+15344`) `0x0D`. A conclusão foi inicialmente registrada como
zero, mas essa leitura externa estava incorreta: faltava o deslocamento host
de 4 KiB do heap físico E/F no Windows. A amostra corrigida em §0.11 lê `0x0B`.
A thread `CommandProcessor::WorkerThreadMain` está esperando trabalho (linha 225),
sem comandos pendentes. O run termina pelo timeout de 120 s, sem exceção não
tratada, mas com 99 linhas STUB, 86 erros e 7 mensagens não implementadas.
Evidência local: `.local/logs/dash17559-fence-state.txt` e os relatórios desses runs.

Naquele momento ainda não havia sido determinado qual pacote atualizava o contador.
`VdGetSystemCommandBuffer` usa placeholders e
`VdSetSystemCommandBufferGpuIdentifierAddress` é vazio; são candidatos, não causas
comprovadas. O próximo diagnóstico é rastrear a emissão/consumo dos pacotes e o
writeback do contador antes de alterar a sincronização. Forçar sua conclusão
esconderia o bloqueio sem validar a renderização; nenhuma alteração assim foi feita.
`DASHUSER:\\ThematicSkin`/`VisionEffect` continuam falhando, sem arquivos inventados.

Builds recompilaram apenas componentes afetados e copiaram DLLs verificadas;
codegen não foi forçado e os arquivos gerados permaneceram iguais.

### 0.11 Primeiro quadro real: DPC e retorno não local (2026-10-12)

**Causa demonstrada da espera:** em `sub_921AFF68`, a partir de `0x921B0008`,
o guest lê o ponteiro em dispositivo `0x40002400 + 11024`, o valor emitido em
`+11036` e a conclusão nesse ponteiro. Espera enquanto
`uint32_t(emitido - alvo) < uint32_t(emitido - concluído)`. A amostra correta
(`.local/logs/dash-fence-alias-state.txt`) tem emitido `0x15`, alvo `0x0D`,
ponteiro `0xFFCA3000`, concluído `0x0B`: `8 < 10` mantém a espera.

`sub_921B0960` emite `EVENT_WRITE_SHD`, initiator 3, endereço físico
`0x1FCA4000`, endian 2. A instrumentação confirmou submissão, consumo e escrita
big-endian de 3, 5, 7, 9 e 11; a CPU vê 11. O endereço físico correto inclui o
deslocamento de 4 KiB e não é o `SCRATCH_ADDR` separado (`0x1FCA3000`).
O primeiro ponto quebrado é anterior à submissão seguinte: no run
`20261012T025839Z-dash-dpc-before`, `KeInsertQueueDpc` enfileira `0x400051C4`,
rotina `0x921C89B0`, contexto `0x400051A4`, argumento `0xDFAA0280`, mas o SDK
nunca despacha a lista. Essa rotina guest deveria registrar o trabalho e
sinalizar o evento da thread de submissão. Não era travamento da GPU do PC.

**Patch 0007:** executa a rotina DPC real no contexto do chamador, em
`IRQL_DISPATCH`, preservando registradores e restaurando IRQL/estado DPC.
Usa o despacho inline do [Xenia Canary 2372a541 como referência](https://github.com/xenia-canary/xenia-canary/blob/2372a54105d9d598ab5bc7ba94a72ef1bea4b271/src/xenia/kernel/xboxkrnl/xboxkrnl_threading.cc).
É HLE de DPC no processador corrente, não um escalonador completo por CPU.
O callback executado não fica preso na lista. O contador não é tocado pelo patch:
o guest envia os comandos e `EVENT_WRITE_SHD` continua fazendo o writeback.
Run `20261012T030254Z-dash-dpc-after`: consumo passa do índice 49, conclusão
ultrapassa `0x0D` e chega a `0x1D`; a inicialização avança.

Dois bloqueios seguintes foram corrigidos separadamente:

- `XUsbcamCreate`, antes uma armadilha local, informa `X_STATUS_NOT_IMPLEMENTED`.
  Não cria handle nem retorna o sucesso incondicional do stub SDK/Xenia. O guest
  executa seu caminho de falha em `0x92368448`; a câmera permanece desconectada.
- O run `20261012T030446Z-dash-camera-unavailable` alcança o tratamento protegido
  de erro da interface, mas termina em `KeBugCheck`: sem identificação do CRT,
  `longjmp` era uma função C++ que retornava ao chamador errado. A análise do
  buffer confirmou `setjmp=0x9291C760` e `longjmp=0x9291C330` (SP+144, CR+304,
  LR+308, registradores não voláteis). Os dois endereços foram adicionados ao
  TOML; o suporte existente do codegen agora emite `ppc_setjmp`/`ppc_longjmp`.
  Não se ignora o erro: ele retorna ao tratador protegido original.

**Resultado visual:** run `20261012T030938Z-dash-jmp-fixed` apresenta conteúdo
real: fundo, blocos e ícones. Durante entradas direcionais e botão A foram
observados painéis diferentes, inclusive o de configurações. Captura local:
`.local/logs/dash17559-real-frame-input.png`. Isso demonstra renderização real
e resposta parcial à entrada, não compatibilidade completa ou lançamento de jogos.

**Limites concretos:** `media:\SegoeXbox-Light.xtt` falha com `0xC000000F`;
esse nome não existe nos inputs locais, e não foi substituído por outra fonte
renomeada. Também houve leitura inválida de `0x110` durante a navegação:
`XamLookupCommonStringByIndex(272)` é um stub (`xam_misc.cpp`) que devolve o ID
residual, tratado como ponteiro UTF-16 por `sub_92188D88` → `sub_921996F8`.
Falta implementar o contrato de strings de sistema (também é pedido o ID 275).
O relatório registra exceção e timeout; não é uma execução sem falhas.
`DASHUSER:` e a região experimental permanecem como antes.

**Validação e consolidação:** o teste DPC sem patch falha no assert upstream
(`20261012T030604Z-dpc-test-unpatched`); com patch passa em 16 asserções.
DPC + câmera: 20 asserções, 2 casos. `ctest -R sdkcheck`: 17/17 aprovados.
Iruka: L1–L5, 309 cores, diferença 5,752, zero stubs/erros/críticas
(`20261012T031549Z-regress-iruka`). Castle Crashers: 45 s, sem crash/erros/críticas,
4 chamadas ao stub `XamShowMarketplaceUI` (`20261012T031646Z-cc-regression-0007`);
a conferência visual dessa regressão foi interrompida pelo Escape físico.
A suíte inteira não foi repetida neste incremento.

A instrumentação de GPU foi removida e o título recompilado. Na árvore final,
as DLLs ao lado do título e na saída do SDK têm hashes iguais: runtime
`f80ab89c1e8aa21da60cfaebd59e497e65536ee04aebfdfd18974ace18509c34`, plugin
`144de23270c1465bc2cc263330feaa4152e3090e0a5427a65078b4df9c0a55cb`.
O run visual citado usou o runtime `9f13a427…57faf0b` com o mesmo patch e
instrumentação temporária. Não houve outra sessão visual após o Escape;
o build final sem instrumentação não tem captura própria.

### 0.12 Navegação, textos e bloqueio da biblioteca (2026-10-12)

O primeiro quadro permanece preservado no commit local `8c4f713`. Os patches
0008–0010 corrigem a fila de teclas do driver MNK, o ABI de data/hora e o retorno
de ponteiro nulo para strings comuns ausentes (sem aceitar confirmações).
`sdkcheck`: **22/22** em `.local/logs/navigation-final-ctest.log`.
Regressões com os patches: iruka L1–L5 aprovado (313 cores, diferença 7,144,
run `20261012T040641Z-regress-iruka`); Castle Crashers apresentou o menu principal
e permaneceu 45 s sem crash, stubs, erros ou críticas
(`20261012T040834Z-cc-regression-navigation`). Ambos encerrados pelo timeout do
supervisor. O teste isolado de Castle Crashers não valida o ciclo pela dashboard.
A suíte inteira não foi repetida.

**Tela real:** menu principal Metro, com transições entre painéis e atividade
contínua de desenho observadas; não apenas um quadro. Espaço chega ao convidado
(chamou `HalOpenCloseODDTray` no bloco do disco) e `3` muda para jogos.
O usuário confirmou controle físico e Shift+setas, depois textos visíveis e
abertura do menu do sistema. Voltar com B/Backspace ainda não foi confirmado
isoladamente. Outros painéis não são declarados funcionais.

**Fonte:** `XamGetLanguageTypeface` era stub: não preenchia o localizador da
fonte alternativa. A integração do título agora escreve UTF-16BE limitado ao
buffer e usa o mapeamento de Xenia Canary `2372a541`, `xam_locale.cc:666`:
JKLatin para os demais idiomas, CLatin para 8, SCLatin para 17. O ponteiro de
patch opcional é nulo, não o ID residual do idioma. Foram copiados, sem renomear,
os arquivos fornecidos em `resources/fonts/Segoe UI (Kinect, Metro)/` (JKLatin,
CLatin), e `versions/17559/metro_v2_retail_full_readme/XenonSCLatin.xtt`, para a
raiz ativa `17559/`. O usuário confirmou os textos no run
`20261012T035706Z-dash-typeface`. A fonte principal `media:\SegoeXbox-Light.xtt`
ainda falha com `0xC000000F`; não foi fabricada nem substituída por arquivo
renomeado. Esta validação comprova o fallback, não a fonte original.

**Biblioteca não corrigida:** o clique real em My Games chamou `XamLaunchURI`
em `LR=0x92190354`, r3=0, r5=5, com a string ASCII
`gameslibrary:_RootTitle=My Games;` em r4 e OVERLAPPED em r6. Evidência:
`20261012T035122Z-dash-uri-trace/rex.log`, 00:52:46.546. Nenhum trabalho era
despachado; o stub deixava r3=0, confundido com sucesso. O diagnóstico temporário
foi removido; o título agora retorna `E_NOTIMPL` sem escrever/sinalizar o
OVERLAPPED. Isso torna a falha honesta, **não implementa o roteador**.
O callback `0x92183C10` registrado em `XamLoaderRegisterLaunchRequestCallback`
prepara a saída do título; não é um receptor de URI e não foi chamado à força.
Falta o encaminhamento validado ao aplicativo interno da biblioteca, antes de
qualquer solicitação do jogo. O ciclo dashboard → Castle Crashers → dashboard
**não foi executado**. O supervisor existente supervisiona processos, mas ainda
não recebe esse pedido da dashboard.

Conferência de inputs: o inventário anterior à organização já listava 123
arquivos em `17559 Full (Readme)` sem `xam.xex`. Os `.xzp` com esse nome são
recursos, não o módulo. A deduplicação de 17559 retirou só cópias idênticas de
`dash.xex` e `shrdres.xzp`. Não foi encontrado `xam.xex` 2.0.17559.0 para conferir
o contrato do roteador; não é uma falha de abertura desse módulo pelo runtime,
que atualmente fornece XAM por HLE. Não se misturaram versões na execução.

Outro bloqueio separado, reproduzido nos runs `dash-navigation-fixed` e
`dash-typeface`: configurações consultam XConfig categoria 7 não implementada,
e `sub_92281F80` copia uma lista com ponteiro residual `0x1C` em
`sub_928113D8`. Continua sem correção. A região experimental `0x8E030000`
permanece inalterada e identificada como hipótese.

### 0.13. Recursos Retail 17559, URI e intro original

Arquivos obtidos anteriormente do update oficial foram preservados com sua
procedência em `versions/17559/retail_system_assets/`. O manifesto do título
associa hashes e papéis: SegoeXbox-Light.xtt é recurso local do perfil;
xam.xex é referência de análise e **não é carregado**; bootanim.xex é módulo
AOT opcional da 17559. Não há recurso global aplicado a outras dashboards.

`launch_uri.cpp` reproduz apenas `gameslibrary:` com flags 5: expansão original
em `0x8199FEE8`, launch data de 1020 bytes (reason 12, usuário, payload em +12)
e notificação `0x80040021`, conforme handler `0x819A0440` do XAM 17559.
Entrega a requisição antes de concluir OVERLAPPED; isso não afirma abertura ou
lançamento de jogo. Outros esquemas/flags continuam sem implementação.
Run `20261012T044046Z-dash-uri-hub`: usuário viu somente Voltar na biblioteca.
O crash posterior em 0x1C foi no caminho de configurações já registrado em §0.12.
Ainda não houve solicitação real de lançamento de Castle Crashers.
Repetição com log debug `20261012T050600Z-dash-library-debug`: clique às
02:06:28.426, notificação entregue a quatro listeners, construção de locators
shrdres e novos listeners da interface. Não aparece chamada de enumeração de
conteúdo nesse caminho. A causa da tela sem conteúdo ainda não está isolada;
não se atribui essa ausência somente à instalação dos jogos.
Conferência adicional do XAM: `0x819A07B0` consulta app FC, mensagem 58003.
Resultado negativo ou 0x1510F1 seleciona a expansão BuiltIn; os demais valores
selecionam Dash.MP.ContentExplorer.lex. O SDK atual retorna erro nessa consulta,
coerente com a expansão BuiltIn usada. Isso não prova que o serviço 58003 esteja
correto; a rota alternativa não foi forçada para tentar esconder a falha.

Intro: `--dash17559_boot_intro=true`; sem a opção continua o boot direto.
O original não tem entry point e expõe três ordinais. Export 1 recebe hmodule
e flags 0, inicializa/renderiza/toca o recurso `audio` embutido. Um thunk e
nove alvos indiretos precisaram de hints de análise, conferidos com as
ferramentas existentes; não houve `--force` nem alteração de instruções.
Run `20261012T045242Z-dash-bootanim-pointers`: imagem, animação e som confirmados
pelo usuário; logo final retido porque faltava a chamada de controle do host.
Export 2(false) pede encerramento gradual. O próprio `0x9800EA10` verifica
tempo >= 6, efeito terminado e hold >= 4.5 antes de limpar/retornar.
Run `20261012T045539Z-dash-bootanim-graceful`: export 1 às 01:56:06.908,
callback gráfico removido às 01:56:17.885, retorno às 01:56:17.941, novo
callback da dashboard às 01:56:17.946; clique real em My Games às 01:56:26.212.
Não há vídeo substituto, avanço de contador ou saída por timeout na transição.
Os dois processos de diagnóstico foram encerrados pelo host depois da observação;
isso não demonstra encerramento normal da dashboard. HalSendSMCMessage permanece stub.

Validação: 23/23 sdkcheck, identidade dos alvos passou; iruka L1–L5 passou
(`20261012T045814Z-regress-iruka`). Castle Crashers 0.0.1.3 executou 45 s
(`20261012T045916Z-cc-regression-system-assets`), sem erro/crítica/crash;
houve duas chamadas ao stub XamShowAchievementsUI durante entrada do usuário.
Foi execução independente: **o ciclo dashboard → jogo → dashboard não ocorreu**.
Todas as versões antigas/betas preservadas continuam candidatas, não compatíveis
por associação com a 17559. A região experimental permanece inalterada.

### 0.14. Segundo perfil: Retail 6717 (Blades)

`titles/dash_6717` usa apenas `versions/6717/blades_retail/dash.xex` e seus
recursos. SHA-256 `f8a51ea30406e3b8737e0cdbdad44867743869d59f58d4bc0f4b83e154cb87d8`,
2.0.6717.0, entry 0x92114CC8. Mesmo runtime, supervisor e scripts de build;
sem XAM, fonte, intro ou região experimental emprestados da 17559.
Nove entradas de análise conferidas permitiram gerar/compilar. Permanece
aviso do writer: salto de unwind 0x9222D200 para epílogo 0x9222D1B0; o código
gerado abortará se alcançar esse caminho. Não foi usado `--force`.

Run `20261012T051619Z-dash6717-first-run`: parou em XUsbcamGetState,
LR=92356FB4. A consulta de câmera desconectada já testada na 17559 agora está
em `titles/common/camera_state.cpp`, ligada pelos dois perfis. Não cria câmera
nem altera o comportamento da 17559. O chamador original 6717 em 0x92356FA0
confere estado 2 antes de inicializar captura, como na 17559.
Run `20261012T051841Z-dash6717-camera-state`: passou dessa consulta, registrou
callback gráfico 0x92129940 e parou no import XAM ordinal **0xAE6**, thunk
0x92739D94, LR=0x9210F1F4. A entrada não existe na tabela do SDK fixado;
a armadilha é explícita. Ainda sem quadro demonstrado ou compatibilidade.
Identidade passou no teste existente `local.integration targets`.
Após extrair a consulta comum, 23/23 sdkcheck passaram novamente. A 17559 foi
recompilada e repetiu a transição em `20261012T052114Z-dash17559-intro-shared-camera`:
intro às 02:21:23.684, retorno original às 02:21:34.870 e callback da dashboard
às 02:21:34.872. Os módulos carregados foram conferidos no processo: runtime,
Xenos e bootanim vieram de `.local/build/titles/dash_17559/` (registro local em
`.local/logs/dash17559-final-loaded-modules.txt`).
Esse diagnóstico foi encerrado pelo supervisor aos 50 s, sem crítica/crash,
mas com os stubs e erros de serviços já conhecidos. O timeout não é validação
de encerramento normal; a transição da intro ocorreu antes dele.

### 0.15. Retail 6717: primeiro quadro, troca de blade e navegação

Boot direto, sem intro. Quatro correções, cada uma de uma parada observada:

| Parada | Correção | Evidência |
|---|---|---|
| armadilha xam **0xAE6**, LR `0x9210F1F4` | `titles/dash_6717/src/curl_bind.cpp`: registra a tabela e não escreve resultado | único chamador passa `r3 = 0x927FC914` (de `0x92588028`): 7 funções do próprio `dash.xex` + `0x18`; o resultado não é lido. As 7 usam o estado de cache `0x928C6BE8` (iniciado por `0x925881D0`, chamado logo depois) sob a seção crítica `0x928E6C98`: a dashboard fornece o cache e o entrega ao XAM. Nome segundo o Xenia Canary `2372a54105d9`: `CurlBindFunctionTable`. O `xam.xex` 17559 não o exporta mais (offset 0) e a dash 17559 não o importa; o `xam.xex` 1888 local não carrega. Nada no XAM do SDK chamaria a tabela: **registrada, não usada** |
| `Call to invalid or unregistered function at 0x9214C560` | `analysis/dash_6717_pointer_targets.toml`: 334 inícios só por ponteiro, mesmo script e critérios da 17559 (§0.8) | `mr r3,r5; b`; amostra de 10 conferida por desmontagem; 13 funções antigas viraram blocos das novas (sem `bl` nem referência); 0 sobreposições `.pdata`; codegen sem `--force` |
| armadilha `XUsbcamCreate`, LR `0x92356E7C` | implementação "não implementado" movida para `titles/common/camera_state.cpp` (17559 e 6717) | 6717 `0x92356E78`: retorno ≠ 0 decrementa a referência de inicialização (`0x9288DD7C`) e volta como status de `0x92356DE0`, como na 17559 |
| `media:\shrdres.xzp` e `media:\XenonJKLatin.xtt`: "device not found" | `\SystemRoot` montado só leitura na pasta do pacote 6717 (`src/dash_6717_app.h`) | a própria 6717 cria `media:` e `dashmedia:` → `\SystemRoot` (log debug, run `…T054514Z-dash6717-media-off-debug`). Uma tentativa de ligar `media:` pelo perfil foi descartada: era desnecessária |

Teste focado: `sdkcheck.dash 6717 xam 0xAE6 records the bound table…` (14 asserções);
o teste de câmera continua passando com a implementação comum. `sdkcheck`: 24/24.

Resultado (run `20261012T054645Z-dash6717-input`, 55 s, sem crítica; DLLs iguais às do SDK):

| Etapa | Situação |
|---|---|
| Processo aberto | sim |
| Quadro apresentado | **sim**: blade verde "Xbox 360" (cartão de jogador, ícones, barra inferior) |
| Interface atualizando | sim: fundo animado; ícone de disco com "?" aparece e some |
| Navegação | `D` (analógico para a direita) troca para a blade **Mídia** (azul, 5 itens); `S` move o destaque para o 2º item. `A` chamou `XamShowGamerCardUI` (stub), sem efeito visível; depois a tela estava de novo na blade verde (causa não atribuída) |
| Interface correta | **não**: nenhum texto aparece |
| Lançamento de jogo | não testado |

Bloqueio do texto: a 6717 abre `media:\XenonJKLatin.xtt` (58 mil tentativas em 45 s)
e o arquivo não está no pacote 6717 (só `dash.xex` e `shrdres.xzp`). É dependência
demonstrada; não foi copiada fonte da 17559 nem de outra pasta. O usuário informou
que, no teste dele, "parece que está funcionando" (relato, não observado aqui).
Também falham `DASHUSER:\DashStyle`, `ThematicSkin`, `VisionEffect` (não fatais).
Regressões: 17559 com boot direto chega ao menu Metro com texto e o mesmo aviso de
`XUsbcamCreate` (run `…T054945Z-dash17559-camshared`); iruka L1–L5; Castle Crashers
45 s sem stubs/erros, menu na captura (run `…T055111Z-cc-regression-6717`).

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

## 0.16. Biblioteca 17559: enumeração e atributos (2026-10-12)

A espera original da biblioteca foi localizada em 922C8330 -> 92272598 ->
92264040: o serviço 92969878 aguardava estado +272=2 (com geração) ou 3.
Faltava a notificação inicial de armazenamento 0x0B. O patch 0012 entrega a
notificação; o handler original 92180388 -> 92263210 sinaliza o evento +36.
Run 20261012T065301Z-dash17559-storage-notify chegou ao estado 3 e a página
passou a mostrar No games found, confirmado pelo usuário.

O patch 0013 corrige a consulta agregada de todos os tipos (FFFFFFFF) e a
leitura de cabeçalho ausente. Castle Crashers 584108B7, versão 0.0.1.3, foi
associado somente ao user_data_root da 17559 por junction para a extração já
existente; o .header do SDK é metadata gerada a partir do pacote LIVE existente,
não um recurso original. Procedência em .local/user/dash17559/castle-crashers-install.json.
A enumeração retorna um item HDD; 922C8378 aceita tipo 000D0000 e título 584108B7.
Mesmo assim o usuário ainda observou No games found.

Bloqueio seguinte demonstrado: 922C61A0 chama XamContentGetAttributesInternal
stub e retorna 80004005. Patch 0014 consulta atributos reais do pacote extraído
(36 bytes big-endian, layout confirmado no XAM original 17559). Teste antes:
6/36 falhas; depois: 36/36. A execução 20261012T072822Z-dash17559-real-content-attributes
avança à preparação XBLA 922C7518 e retorna 80070003 (caminho não encontrado).
A etapa de atributos não é mais o stub. 27/27 sdkcheck passaram.

A estrutura agregada estendida de 512 bytes ainda não está implementada
integralmente (SDK entrega 328). Não houve preenchimento fictício dos metadados,
licença forçada, salto de espera ou alteração das hipóteses gráficas.
Ciclo pelo menu ainda não executado; 6717 mantém seu perfil e pendências.

O patch 0015 resolve esse 80070003: a abertura interna usava o XUID do perfil
em vez do proprietário zero (conteúdo compartilhado) do registro enumerado.
O XAM original lê esse proprietário em 8167F690. Teste antes: 2/40 falhas;
depois: 40/40, incluindo a abertura e o caminho real montado.
Run 20261012T073813Z-dash17559-shared-owner: 922C61A0 retorna 00000000;
usuário confirmou Castle Crashers visível. Às 04:38:51 a confirmação do item
chegou a XamContentLaunchImageInternalEx, ainda stub. Esse é o bloqueio atual,
não mais enumeração ou montagem. O jogo ainda não iniciou pelo menu.

Consolidação 0011–0015: 27/27 sdkcheck; iruka 30 s (20261012T074255Z-iruka-library-0015)
e Castle Crashers 45 s (20261012T074353Z-cc-library-0015), ambos sem stubs, erros
ou críticas, terminados pelo timeout. DLLs carregadas conferidas pelos hashes
dos relatórios. Cobertura de processo/log, sem nova validação visual L4–L5.
Diagnóstico temporário retirado e dashboard recompilada sem os wrappers.

## 0.17. Primeiro ciclo pelo menu original (2026-10-12)

Patch 0016: XamContentLaunchImageInternalEx monta o conteúdo existente como
XSYSLAUNCH e registra o executável antes de terminar o título. Contrato conferido
no XAM local 17559 (816F00E8 -> 816EFED0). Suporta HDD extraído, imagem na raiz
e flags 0; outros modos falham explicitamente. Não força licença.
Teste antes: 4/56 falhas; depois: 56/56. 27/27 sdkcheck e 38/38 x360n passaram.

O comando x360n session titles/dash_17559/dash_17559_manifest.toml lê as
associações x360n dos manifestos existentes, valida caminho e SHA-256 do XEX,
e reutiliza run para cada processo. O canal transfere os bytes reais de launch
data; não executa um caminho arbitrário recebido do convidado. Fechamento da
janela ou saída da thread principal do jogo retornam ao perfil que iniciou a
sessão; timeout/crash não provocam retorno automático. Boot direto preservado.

Execução real, confirmada visualmente pelo usuário:
- 20261012T075539Z-session-0-dash_17559: clique em Castle Crashers, às 04:56:13,
  solicita XSYSLAUNCH:\default.xex, flags 0; dashboard encerra com código 0.
- 20261012T075615Z-session-1-castle_crashers: versão 0.0.1.3, importa 1020 bytes,
  inicializa GPU/áudio; sai com código 0 após 47511 ms, sem stubs/erros/críticas.
- 20261012T075704Z-session-2-dash_17559: menu retorna, confirmado pelo usuário;
  sessão encerrada normalmente depois.

A DLL carregada pelo jogo foi verificada em .local/build/titles/castle_crashers;
os relatórios guardam hashes. Permanece a limitação de janelas separadas por
título; a janela persistente e o Guide original ainda não estão implementados.
XamShowGuideUI continua stub; XamLoaderLaunchTitle(NULL) ainda faz assert.
O retorno observado foi GuestExit, não uma chamada validada pelo Guide.
O mutex do escritor evita disputa entre saída do guest e fechamento da janela.

Regressão iruka com 0016: 20261012T080209Z-iruka-launch-0016, 30 s,
0 stubs/erros/críticas, encerrada por timeout; sem nova aferição visual.

## 0.18. Janela persistente e regressão de entrada (2026-10-12)

O supervisor mantém um HWND e hospeda as superfícies SDL/D3D12 reais dos títulos
(patches 0017–0018); processos continuam separados. Inicialmente teclado e controle
pararam: WIN_UpdateFocus comparava o HWND filho com GetForegroundWindow, que aponta
para o supervisor. SDL também descarta joystick sem foco de teclado. Corrigido com
foco real no filho e raiz ativa; a publicação do filho ocorre após ShowWindow.
Usuário confirmou os dois dispositivos e o ciclo na mesma janela.

Evidência: 20261012T082937Z-session-0-dash_17559 recebe foco e solicita
XSYSLAUNCH:\default.xex por XamContentLaunchImageInternalEx;
20261012T083006Z-session-1-castle_crashers importa 1020 bytes e sai em 35217 ms
com código 0, 0 stubs/erros/críticas; 20261012T083043Z-session-2-dash_17559 retorna.
Os três registram parent=3868796, com superfícies filhas distintas.
Esse retorno foi GuestExit: ainda não comprova comando Xbox Home/Guide.
Fechar a janela principal cancela a plataforma; --separate-windows mantém diagnóstico.

Validação: 39/39 testes x360n no host; 28 sdkcheck passaram e 1 foi ignorado porque
o Windows negou ativação de foco ao probe. Não tratar esse skip como teste de foco
aprovado. A confirmação de entrada no alvo veio do usuário e dos eventos reais.
A execução restrita do teste de handoff falhou na escrita/rename temporário; no
host passou. Diagnósticos temporários de perfil foram preservados em .local/tmp
e retirados do título.

Perfil: Settings chamou XamShowGamerCardUI às 05:32:30.883, ainda stub. XAM 17559
original 816C40F0 despacha app FE, mensagem 21008, bloco de 544 bytes. O SDK não
implementa esse handler. Não houve XamShowSigninUI/UIp nessa tentativa; cartão de
perfil não é seleção de usuário. UserProfile ainda fixa User/XUID/state=1; não há
entrada/saída ou persistência real validada. Nenhuma conta/save foi alterada.

Regressão iruka 0018: 20261012T083614Z-iruka-child-focus-0018, 30173 ms,
0 stubs/erros/críticas, saída por timeout. Cobertura de processo/log; sem nova
captura visual L4–L5. Dashboard recompilada sem os diagnósticos temporários.
