# Dashboard original: diagnóstico

Retail **1888** (18/10/2026): Blades original, textos, troca Games/Media/System e
Console Settings demonstrados com teclado; controle físico confirmado pelo
mantenedor. Perfil Release próprio selecionável no launcher, com snapshot de 22
originais e dados isolados. A intro original também renderizou animação/logo,
com som confirmado e transição à Blades em 11,867 s; habilitada pelo launcher.
[Contrato e validação da intro](reports/dashboard-1888-intro-2026-10-18.md).
Guide/perfis/serviços/jogos não certificados; recurso
localizado pt-BR ausente. [Procedência, SHA-256, testes e limites](reports/dashboard-1888-2026-10-18.md).
As seções históricas abaixo tratam da 17559, salvo versão explicitamente indicada.

Estado consolidado em 2026-10-15: Retail 17559 renderiza intro/menu e My Games,
com ciclos Castle/Forza demonstrados. C7.7 (§0.31) demonstrou criação, troca e
persistência de perfis locais; C7.8 (§0.32) mantém isolamento dos originais.
Saves por perfil têm [testes C8](save-storage.md), sem aceite comercial.
Guide/Xbox Home foi demonstrado no [ciclo Forza](reports/titles/validation-forza-horizon.md);
isso não certifica todos os serviços da interface. A região zerada de §0.10
permanece hipótese experimental. As seções numeradas são registros históricos:
falhas e pendências valem para o incremento datado, salvo atualização posterior.

Referências: ReXGlue v0.10.0 `f5337cdc` (caminhos relativos a
`external/rexglue-sdk`) e Xenia Canary `04f85416e0f8` (consultado só como
referência de comportamento; não é usado como executor).

## 0. Pacote 17559

Arquivos em `.local/inputs/dashboards/17559/runtime/` (ignorado pelo git). Nenhum foi
alterado (hashes reconferidos); hashes em `.local/inputs/dashboards/metadata/17559/17559.sha256`,
identidade em `.local/inputs/dashboards/metadata/17559/17559-*.identify.json`. Nenhum arquivo de
outra versão foi usado no alvo ativo. A coleção fornecida posteriormente está em
`.local/inputs/dashboards/<versão>/<variante>/`, e as fontes em
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
`scripts/analysis/xex-imports.ps1`, `scripts/analysis/xex-module-strings.ps1`,
`scripts/analysis/xex-locator-strings.ps1`, `scripts/analysis/check-function-bounds.ps1`,
`scripts/analysis/check-pdata-overlap.ps1`. Saídas em `.local/evidence/runs/dash-artifacts/`.

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
SHA-256 em `.local/evidence/runs/dash-artifacts/dash-resources.csv`).

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
(`scripts/analysis/xex-locator-strings.ps1`: 747 textos `<esquema>://`), código que usa
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

**Limites conferidos no código gerado** (`scripts/analysis/check-function-bounds.ps1`):
30/30 iguais aos documentados. A única diferença inicial era minha: em
`0x923766B0` incluí um `blr` inalcançável depois de um `bctr`.

**Destinos dinâmicos** (os 12 thunks): o endereço de entrada é estático; o
destino é lido de duas tabelas em `.data`, zeradas na imagem e escritas só por
`0x9218B4A8` (tabela A, `0x92992C88–0x92992D0C`) e `0x927B12B0` (tabela B,
`0x92A77A28–0x92A77AB0`, zerada de novo por `0x927B16A8`). Ambas fazem
`XdfLoadXexFromCache("dashnui.xex", 8, 0, &handle)` e resolvem cada slot com
`XexGetProcedureAddress` por **nome** (`NuiIdentityEnroll`,
`SpeechOnlineRecognize`, …; mapa slot→nome em
`.local/evidence/runs/dash-artifacts/`). Nenhum destino foi fixado. Com o SDK atual o
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
  --gpu_plugin=xenos --game_data_root=<raiz>\.local\inputs\dashboards\17559\runtime `
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
execução, `scripts/analysis/check-pointer-targets.ps1` classifica todos os endereços de código
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
Evidência local: `.local/evidence/logs/dash17559-fence-state.txt` e os relatórios desses runs.

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
(`.local/evidence/logs/dash-fence-alias-state.txt`) tem emitido `0x15`, alvo `0x0D`,
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
`.local/evidence/logs/dash17559-real-frame-input.png`. Isso demonstra renderização real
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

O primeiro quadro permanece preservado no commit local `0195954`. Os patches
0008–0010 corrigem a fila de teclas do driver MNK, o ABI de data/hora e o retorno
de ponteiro nulo para strings comuns ausentes (sem aceitar confirmações).
`sdkcheck`: **22/22** em `.local/evidence/logs/navigation-final-ctest.log`.
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
`.local/evidence/logs/dash17559-final-loaded-modules.txt`).
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
.local/inputs/dashboards/<versão>/   (ignorado pelo git)
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

Entrada: uma versão autorizada da dashboard em `.local/inputs/dashboards/<versão>/`.

1. `x360n identify --json` em cada XEX: SHA-256, flags, title id (esperado
   `FFFE07D1` para `dash.xex`), versão, formato, bibliotecas importadas.
2. `rexglue init --project-name dash_<versão> --xex-path <dash.xex> --project-root .local/build/scaffolds/dash_<versão>`
   e `rexglue codegen` **sem `--force`**; guardar log e código de saída.
3. Produzir a matriz de imports: cada ordinal usado → implementado / stub /
   ausente no SDK v0.10.0.
4. Listar módulos carregados dinamicamente (`XexLoadImage`) e recursos abertos.

Critério de saída: relatório com números reais. Iniciar a dashboard fica para D2.
Resultado para a 17559: passos 1–4 feitos (§0.2–0.4; o projeto ficou em
`titles/dash_17559`, fora do scaffold temporário). D2 (iniciar) parou no setup do
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

## 0.19. Contrato de retorno original (2026-10-12)

Patch 0019 distingue XamLoaderLaunchTitle(NULL, flags), XamLoaderTerminateTitle e
retorno simples da thread. O protocolo preserva flags e launch data, inclusive na
volta; fechar a janela do host encerra a plataforma. O retorno simples ainda é
identificado como política do supervisor, sem alegar pedido Xbox Home/Guide.

Castle Crashers usa XLNI_DASH_ARCADE em 820B6908, convertido em ponteiro nulo e
flag 0x100. No XAM original 17559: export 1A4=816A1820 ->816A0D70 ->816A0798;
816A0800..816A0840 produz 1020 bytes, reserved=0, reason=23, user=FF. O perfil
17559 aceita esse sinalizador e prepara esse contexto para o código original da
dashboard; o registro recebido do jogo permanece intacto. Outras versões não
herdam o tratamento; flags desconhecidos falham explicitamente.

A sessão 17559 agora pede intro original na inicialização. No retorno, o título
reconhece o tipo de handoff e não instala o bootstrap da intro. Boot direto via
x360n run sem --dash17559_boot_intro continua disponível. O XAM original segue
como referência, não como módulo executado. Perfis continuam no usuário fixo do
SDK; não há nova alegação de login, logout, persistência ou contexto de usuário
selecionado. O bloqueio Settings/XamShowGamerCardUI FE/21008 permanece.

Na regressão apareceu double-free do ICC filename ao destruir a janela filha:
CleanupWindowData já o liberara, mas uma reentrada de foco chamava
WIN_UpdateWindowICCProfile. Patch 0020 recusa foco em janela is_destroying.
A pilha está em .local/evidence/logs/child-focus-debug.log. Depois: 37 asserções de foco e
encerramento passaram; consolidação 31/31 sdkcheck e 40/40 x360n, sem skips.
Testes focados de retorno/URI: 115 asserções em 4 casos.

Ciclo executado e confirmado pelo usuário, sem fechar pelo X:
- `20261012T085507Z-session-0-dash_17559`: intro original de cerca de 11 s;
  My Games chama XamContentLaunchImageInternalEx para Castle Crashers,
  XSYSLAUNCH:\default.xex, flags 0, às 05:55:35.
- `20261012T085537Z-session-1-castle_crashers`: importa 1020 bytes, executa o
  jogo e às 05:56:10 chama XamLoaderLaunchTitle(NULL, 0x100). Sai com código 0
  após 33617 ms; 0 stubs/erros/críticas, dois avisos de shader já existentes.
- `20261012T085612Z-session-2-dash_17559`: importa o pedido Dashboard, flags
  0x100, prepara reason=23/user=FF e retorna ao menu sem executar a intro.
  Usuário confirmou o retorno sem intro. Os três usam parent HWND 12650972.

Os registros de ida e volta preservam os mesmos 1020 bytes, SHA-256
`403e56dcadd8ac10eb9c7d09862ee1551c12ddc32fba578986b8c6f47bca5e5f`;
o registro do jogo não é sobrescrito pela tradução de contexto da dashboard.
Runtime das duas recompilações e saída do SDK idênticos: SHA-256
`15cf617ef08011d723767d3909dab625fba1c3064c3fed0b631f78c6e1961604`.
O caminho da DLL efetivamente carregada pela dashboard foi conferido no processo.

Regressão iruka: `20261012T085714Z-iruka-original-return-0020`, 30218 ms,
0 stubs/chamadas não implementadas/erros/críticas. Encerrada por timeout;
valida execução e log, sem nova captura visual L4–L5 ou saída normal.

Limite visual relatado pelo usuário após esse ciclo: há corte seco ao lançar um
item da biblioteca. Mesmo HWND não garante continuidade: o processo da dashboard
destrói sua superfície antes da criação da superfície do jogo; o host usa fundo
preto. O log encerra a dashboard às 05:55:35.506 e anexa a superfície do jogo às
05:55:38.539. Essa troca não está aprovada como apresentação original. O SDK
VdPersistDisplay também não preserva pixels (somente aloca uma estrutura e retorna
1); verificar seu uso real nesse caminho antes de atribuir o corte a essa API.

Diagnóstico posterior `20261012T090847Z-session-0-dash_17559`, após o usuário
repetir o lançamento: VdPersistDisplay aparece somente às 06:09:03.294, caller
921A6148 (inicialização), não entre o clique e ContentLaunch às 06:09:19.407.
Não está demonstrado que essa API seja a causa direta do corte. A dashboard
registrou quatro callbacks ExRegisterTitleTerminateNotification; o SDK os armazena
em terminate_notifications_, mas TerminateTitle não os percorre. Entre eles estão
921B6160/921B6770 (registro gráfico) e 92187FA8 (encerramento da dashboard).
A referência Xenia local também deixa a execução desses callbacks comentada, com
ABI e sincronização pendentes; não serve como implementação pronta para copiar.
O callback 921B6160 espera o ponteiro de registro em r3 para se remover: chamá-los
sem argumentos, como no trecho comentado, estaria incorreto.

O limite demonstrado é a desmontagem imediata pelo HLE e a perda da superfície
entre processos. Ainda falta implementar/verificar o contrato de saída original
e a apresentação durante a transição. Não foi acrescentado fade, screenshot de
substituição ou espera arbitrária. Instrumentação temporária apenas encaminhava
os exports originais; preservada em .local/tmp/transition_diag_tmp.cpp e retirada
do build ao concluir a coleta.

### 0.20 Preparação original do lançamento e reinicialização gráfica (2026-10-12)

Patches 0021–0023 corrigem três contratos, **sem resolver ainda o corte visual**:
- TerminateTitle executa os registros de encerramento antes de cancelar threads,
  com o endereço do registro em r3, prioridade decrescente e remoção por registro.
  As quatro rotinas da 17559 completaram; o usuário confirmou que isso sozinho
  não eliminou o corte (`20261012T092245Z-session-0-dash_17559`).
- XamLoaderRegisterLaunchRequestCallback deixa de ser stub. ContentLaunch lê
  título/base_version do XEX realmente selecionado e chama o callback registrado.
  A estrutura big endian de 16 bytes e o sinal interno 0x200 seguem o XAM original
  17559 em 8169D0C8/8169E158; flags do chamador continuam separados. O contexto de
  processo de sistema ainda não é implementado e retorna não suportado.
- A passagem bootanim → dash expôs leitura de comandos inválidos: a inicialização
  do ring buffer alterava endereço/leitor em paralelo à thread gráfica e mantinha
  o índice de escrita anterior. A reinicialização e o writeback agora são enviados
  à thread gráfica e aguardados; os índices são zerados juntos ao iniciar a nova
  fila. A fila de funções do CP também ganhou exclusão mútua. Nenhuma espera do
  convidado foi pulada ou contador de conclusão avançado artificialmente.

Tentativa com falha visual: `20261012T093417Z-session-0-dash_17559`. A intro
retornou às 06:34:33.539, mas logo vieram overflows do ring buffer; o usuário viu
a imagem da intro parada. Após 0023, `20261012T093836Z-session-0-dash_17559`
reinicializou a fila anterior com read=write=3313, sem esses overflows, e abriu My Games.
Às 06:39:01.503 chamou 92183C10 com title=584108B7/base=00000103/behalf=1.
O caminho original 92183B18 → 92182120 → 921B1CA8 chegou a VdPersistDisplay,
caller 921B1E84, descriptor buffer=FDEBA000, 1280×720, formato 28280106.
O callback retornou display_prepared=1; isso **não comprova preservação no host**.

Castle Crashers (`20261012T093903Z-session-1-castle_crashers`) executou e pediu
XamLoaderLaunchTitle(NULL, 0x100), com 1020 bytes. Terminou com exit 0 e zero
stubs/erros/críticas. A dashboard retornou em `20261012T093956Z-session-2-dash_17559`,
contexto reason=23/user=FF, sem intro. O usuário confirmou corte persistente e
estalo de áudio; nenhuma correção de áudio foi presumida ou aplicada.

Bloqueio agora delimitado: VdPersistDisplay ainda não preserva o buffer solicitado;
o host destrói a superfície antes de carregar o jogo. No XAM original, 8169CE20
consulta o sinal interno 0x200, usado pela rotina 816C90F0–816C95B0. Seu trecho
816C941C–816C94F4 aplica valores de gamma com fatores 32/32 até 0/32, sob condições
do sistema e esperas em eventos. Esse código do XAM **não é executado** pelo runtime
atual. Reproduzir só um fade temporizado no host não atenderia esse caminho.
Disassembly e imports ficam em `.local/tmp/xam-display-transition*.txt` e
`.local/tmp/xam-transition-imports.txt`. XAM continua referência, não módulo global.

Validação: 32/32 sdkcheck e 40/40 x360n passaram; teste focado cobre registro,
remoção, ordem, metadados e rejeição de XEX sem execution info. Regressão iruka
`20261012T094856Z-iruka-transition-0023`: 30 s, zero stubs/erros/críticas, timeout
diagnóstico; não houve nova validação visual por captura. A instrumentação de
VdPersistDisplay foi retirada do build. Os avisos de biblioteca bootanim ainda
carregada/liberação de heap no teardown já ocorriam na tentativa anterior a 0022.

### 0.21 Imagem persistida e drenagem de áudio (2026-10-12)

0024 atende o descritor real de VdPersistDisplay e retém sua alocação convidada;
remove a alocação fictícia de 64 bytes. Após a preparação original/sinal 0x200,
o perfil 17559 implementa em HLE o loop XAM 816C9424–816C94C4: 33 LUTs RGB16,
fator (32-step)/32, apresentadas pelo backend existente e cadenciadas pelo vblank
convidado. Executa as rotinas D3D já recompiladas 92875B80 (cabeçalho), 921A8E60
(gamma atual) e 921B3978 (conversão da LUT), correspondentes às do XAM. Não é
execução do módulo XAM completo nem fade por opacidade/temporizador do host.
Nenhum contador de conclusão do convidado é forçado. Falha de apresentação
impede o lançamento; não retorna sucesso para encobri-la.

Escopo: 17559/descritor 28280106, D3D12 no Windows x86-64; outras versões não
herdam a sequência. Vulkan não foi exercitado. A imagem permanece no processo
atual até o ponto preto; não há transferência geral de framebuffer entre
processos. Recuperação da interface após falha intermediária permanece limitada.

Execução 20261012T102117Z-session-0-dash_17559: intro termina e preserva imagem;
My Games prepara Castle Crashers às 07:21:40.546, preserva FE272000/1280×720,
e conclui as 33 apresentações às 07:21:41.125 antes de desmontar.
20261012T102143Z-session-1-castle_crashers recebe 1020 bytes e pede retorno
XamLoaderLaunchTitle(NULL,0x100) às 07:22:11.494; exit 0, zero stubs/erros/críticas.
20261012T102213Z-session-2-dash_17559 importa o contexto e retorna sem intro.
Usuário: “parece que melhorou”; ainda não é aprovação integral da fidelidade.

O diagnóstico mostrou descarte de 8 frames convidados/1792 bytes após a intro
e 256/1536 bytes de saída nas trocas. 0025 desliga o callback de preenchimento,
processa a fila pelo conversor/mix normal, faz flush e aguarda esvaziamento antes
de fechar o stream. Aguarda também um período do buffer do dispositivo; isso não
é confirmação física de reprodução. Limite de 2 s registra falha, não sucesso.
Sem fade de áudio. Teste silencioso: antes 0/64 frames consumidos; depois 64/64.
Testes focados: 164 asserções/6 casos. Regressões: 35/35 sdkcheck, 40/40 supervisor.
Logs em .local/evidence/logs/audio-drain-{before,focused}.log e transition-sdk-regressions.log.

Regressão iruka 20261012T103413Z-iruka-transition-0025: 30207 ms até timeout
diagnóstico, zero stubs/mensagens não implementadas/erros/críticas. Sem nova
captura visual L4–L5. Os três títulos foram recompilados sequencialmente; o
processo da nova dashboard carrega rexruntimerd.dll/rexgpu-xenosrd.dll da pasta
.local/build/titles/dash_17559. Nenhum recurso proprietário foi alterado.

Nova execução com 0025: 20261012T103500Z-session-0-dash_17559 drena 8 frames
ao terminar a intro; às 07:35:33.164 termina as 33 apresentações e atende o
pedido original de lançamento. Às 07:35:33.478 drena 1536 bytes pendentes, sem
timeout. 20261012T103535Z-session-1-castle_crashers importa 1020 bytes e inicializa
GPU/áudio/controle. Às 07:36:56.812 o jogo pede XamLoaderLaunchTitle(NULL,0x100); às 07:36:56.971
drena 768 bytes de áudio. Exit 0 em 81622 ms, zero stubs/erros/críticas.
20261012T103658Z-session-2-dash_17559 importa os 1020 bytes, kind=4/flags=100,
e retorna sem intro no mesmo HWND 18484624. Usuário confirmou: “voltaram a
responder, nao ouve mais cortes”, após relatar entrada temporariamente ausente.
Não foi isolada a causa dessa perda temporária; não houve patch de input neste
incremento. A percepção de ausência de cortes está confirmada para esse ciclo,
sem alegar equivalência audiovisual medida contra hardware ou outras versões.

### 0.22 Pedidos de UI de perfil e bloqueio do serviço original (2026-10-12)

Clique real em Settings/Profile: XamShowGamerCardUI, não seleção/Guide. Na revisão
atual o export era stub; XamShowSigninUI retornava sucesso e emitia SIGNINCHANGED
e UI-closed sem abrir interface nem alterar usuário. Patch 0026 encaminha os
blocos originais BE à aplicação FE: 21008/544 bytes para cartão e 21001/20 bytes
para seleção. Falhas seguem a conversão original de 816C16C8 (1627; OOM=14).
Nenhuma notificação de mudança de usuário é emitida só por solicitar UI.
Teste antes: 10/11 asserções falham; depois 11/11 passam. SDK: 36/36 passam.

Execução 20261012T104941Z-session-0-dash_17559, 07:50:04.771 e 07:50:28.674:
FE/21008, arg2=220, resposta 80004005, resultado 1627, User/state=1. Usuário
confirma que nada abriu. A falha seguinte demonstrada é o handler de UI ausente
em XamApp::DispatchMessageSync; o fluxo de entrar/sair **não está funcionando**.

Referência local: XAM 816C7378 despacha signin.xex via 816CDD58; 816C78C4 despacha
gamerprofile.xex pelo mesmo serviço. O loader verifica estado de UI em
81AA20E0+B0/B4 antes de abrir o módulo. signin registra callback 90105000 por
XamRegisterSysApp (90105170), também stub no SDK, e importa XuiInit,
XuiRegisterClass, XuiSceneCreate etc., ainda stubs. Isso exige hospedar a aplicação
e sua interface original; carregar o arquivo sozinho não implementa o serviço.
Os arquivos já estão no pacote oficial extraído, ambos 2.0.17559.0:
.local/tmp/system-assets-17559/work/su-extracted/$flash_signin.xex
(SHA256 86695c6c36250cc4b4addb7ccc07d15d54492b352c979cf5c0934804109a6130) e
$flash_gamerprofile.xex (a99fb2ae20c4b3ffab9fa5f6a73e5766b49fbed5716e8b4d3752d1b97b83aad4).
Sem novo download ou substituição de recursos. Não foram carregados no runtime.

UserProfile ainda fixa User/XUID/state=1; não houve criação, login, logout ou
persistência validados. Saves e perfis existentes preservados. Guide é outra
rota, não exercitada nesta tentativa. Nenhum item de perfis foi marcado concluído.

Regressão real: My Games inicia Castle Crashers às 07:50:36.255 após 33 frames
da transição; 20261012T105038Z-session-1-castle_crashers pede retorno às
07:51:07.323, drena áudio e encerra. 20261012T105109Z-session-2-dash_17559
importa 1020 bytes/kind=4/flags=100 às 07:51:13.822, sem intro. O usuário então
fechou a janela. A dashboard mantém os stubs/erros de serviços já conhecidos.

Iruka: 20261012T105152Z-iruka-profile-ui-0026, 30258 ms, timeout diagnóstico;
zero stubs/mensagens não implementadas/erros/críticas. Sem captura visual nova.
O patch cobre submissão síncrona e propagação da falha, não as permissões,
ativação/fechamento nem o ciclo de vida completo do serviço de UI original.

### 0.23 Cena original de perfil criada; composição ainda pendente (2026-10-12)

Patches 0027–0034 e binding exclusivo da 17559: registro/desregistro de aplicações
com identidade do módulo, alocadores virtuais, tabela AOT no host para evitar a
colisão de memória com a dashboard e carregamento restrito de XEX de recursos.
O XAM original é usado somente pelas rotinas XUI explicitamente ligadas, sem
executar seu entrypoint nem substituir os serviços de usuário do SDK. A skin
huduiskin e gamerprofile pertencem ao conjunto 17559; signin ainda não é carregado.

O erro 8007000E do HUD foi localizado em XamAllocEx(tag=20160000,
flags=01000000,size=4096): o alocador interno acessava um pool XAM não iniciado.
A ponte agora usa a alocação real e pareada do SDK; outras classes permanecem
explicitamente não implementadas. XuiGamerCard também usava o alocador interno;
a ponte de XamAlloc e os hints dos métodos originais da vtable resolvem sua criação.

Run 20261012T134511Z-profile-ui-scene-layout, 10:45:33: bootstrap, skin, fundo HUD,
inicialização gamerprofile e GamerCardScene.xur retornam 0. Raiz 00010057,
DC 300BE000; vínculo original 81694FF8 e layout 817BCAA0 retornam 0.
**Ainda não há quadro visível de perfil.** A tentativa desmonta a cena e retorna
falha explicitamente: falta o host contínuo de atualização, composição e entrada.
O worker original 816CCF00 prepara o contexto D3D antes de 81695098; chamar apenas
o render da cena sobre o dispositivo compartilhado não reproduz esse contrato.
Entrar/sair/criar perfil e persistência continuam pendentes; nenhum sucesso ou
notificação de mudança de usuário foi fabricado. Hipóteses anteriores inalteradas.

Validação: 42 testes SDK / 1183 asserções; grupo focado 8 / 124. Supervisor
40 / 20292 passou fora do sandbox, após duas falhas de escrita nos temporários
na primeira execução restrita (registradas separadamente). Iruka recompilado:
20261012T134747Z-regress-iruka, L1–L5 passam, 326 cores, diferença 6,956, zero
stubs/erros/críticas, encerrado por timeout diagnóstico.

Ciclo com Castle Crashers recompilado: 20261012T134846Z-session-0-dash_17559
termina intro original, exercita Perfil e solicita o jogo 584108B7 às 10:49:28
após os 33 quadros da transição. 20261012T134932Z-session-1-castle_crashers
recebe 1020 bytes e pede retorno original às 10:50:20, flags 100.
20261012T135022Z-session-2-dash_17559 recebe kind=4/flags=100 e os mesmos 1020
bytes sem repetir intro. Usuário confirma entrada/transição/retorno funcionando.
Caminhos dos runtimes carregados conferidos nos processos; hashes de executáveis
e DLLs ficam nos report.json de cada run. Não é aprovação do fluxo de perfis.

### 0.24 GamerCard original visível, navegável e com fechamento (2026-10-13)

C7, primeira fatia: Settings/Profile → FE/21008 → `gamerprofile.xex` →
`GamerCardScene.xur` agora permanece ligada ao HUD original e aparece sobre a
dashboard. `xam_ui.xex` continua restrito às rotinas selecionadas, sem entrypoint,
segundo kernel, gerenciadores globais ou janela adicional. Sign-in/Guide e criação
de perfil são contratos distintos e não foram habilitados neste incremento.

O ponto de composição é o `XuiPresent` da dashboard (9221CD90), somente dentro
de seu quadro 92186D90, depois de RenderEnd e antes do resolve/swap original.
O HUD 81695098 compartilha o dispositivo/contexto existentes. Cada quadro chama
o callback original 8000000A, os timers 817DA398 e animações 817AB6D0 com delta
em **milissegundos**, conforme 816C8108. Sem polling independente, sincronização
bloqueante ou cópia de imagem. O host também inicializa o lock original do cache
de imagens, registra a fonte original resolvida por 81699810/817DEA18 e inclui
o método 8177C980 da vtable do DC, necessário a partir do segundo quadro.

Patch 0039: sessão compartilhada com módulo proprietário/usuário, visibilidade,
habilitação de input e fechamento diferido. `XamInputGetKeystrokeHud` recebe os
eventos da infraestrutura de entrada existente; GetState/GetKeystroke/Ex da
dashboard ficam neutros para esse usuário enquanto a UI possui o foco.
`XamIsUIActive` inclui essa sessão. `XamEnableSystemAppInput`, `XamAppUnloadSelf`
e o caso flags=0 de `XamAppUnloadStack` têm efeitos reais; casos sem sessão ou
flags não implementadas falham explicitamente. `GetTickCount` usa o relógio
guest, permitindo o polling original de notificações. Nenhum SIGNINCHANGED é
emitido: apenas XN_SYS_UI quando a visibilidade muda.

O pedido FE é copiado e permanece válido até fechar. Após o pedido original de
saída, o host desliga a cena, limpa a referência HUD, chama 80000005, desregistra
o recurso, executa o detach original e descarrega o módulo. O detach usa
ExecuteTrap e o bridge não declara `restrict` sobre um contexto que também é
acessado por ThreadState. Isso corrige o crash reproduzido ao devolver r3=1 ao
XuiPresent da dashboard. Falhas de layout/render também liberam a sessão.

Evidência real: `.local/evidence/runs/20261013T144103Z-c7-context-safe/`, três cenas
criadas, compostas continuamente e fechadas após 1817, 3645 e 1309 quadros.
Na segunda abertura, o teclado moveu View Games → Edit Profile; após a terceira
saída, Settings recebeu o comando e moveu Profile → Preferences. Sem crash.
Capturas: `.local/evidence/logs/c7-reopened-navigation.png` e
`.local/evidence/logs/c7-close-dashboard.png`. Esses contadores comprovam atualização,
não constituem benchmark de desempenho nem medição de vazamentos.

Limites: nome, estatísticas e imagem do usuário ainda não estão completos;
subpáginas de gerenciamento não foram aprovadas. SDK mantém o usuário fixo.
Trabalho futuro, fora desta tarefa encerrada: ligar FE/21001 à aplicação original `signin.xex`
com o contrato correspondente e introduzir estado real de usuários no núcleo,
antes de criação/persistência/sign-out. Não reutilizar essa ABI em outra dashboard.

Encerramento C7 em 2026-10-13: usuário confirmou três ciclos e 107 asserções
aprovadas. Nenhum novo teste, build ou jogo foi executado para encerrar a tarefa.
O commit `4cc7303` do repositório principal já preserva a mudança do SDK em
`0039-system-ui-session.patch`, com SHA-256 e hashes antes/depois no lock,
mantendo o pin `f5337cdc`. Não depende de commit local exclusivo do submódulo.

### 0.25 Sign-in original: integração C7.1 (2026-10-13)

Implementado no perfil 17559: FE/21001 seleciona `game:\system\signin.xex`
(20 bytes, usuário em +4), preservando o bloco original de `XamShowSigninUI`:
`{0, FF, user_count, flags, 0}`. O callback `90105000`, registrado pelo DLL
entrypoint em `90105170`, usa slot FF e recurso 3. O host confere essa identidade,
copia o pedido e usa as mensagens originais de initialize/prepare/create/update/
destroy, o mesmo HUD/DC, apresentação e fechamento diferido da GamerCard.
O sucesso da solicitação só é devolvido após criação, vínculo e layout da cena;
erros continuam propagados. Não há segundo kernel/XAM, perfil inventado,
SIGNINCHANGED, nova janela, cópia gráfica ou laço de polling.

Contrato conferido por desmontagem local: XAM `816C19C8`, `816C1868` e
`816C7378`; Sign-in `90105000`, `90105198`, `90106CF8` e `90106F80`.
A falha de initialize do Sign-in já destrói seu objeto em `901050FC–90105124`;
o host não envia destroy novamente nesse caso. A cena de saída é zerada antes
da chamada, impedindo que um retorno sem raiz seja aceito.

Sign-in e GamerCard têm base `90100000`; não podem ser carregados juntos nem
na análise AOT. O primeiro build demonstrou a colisão no gerador (267 chamadas
não resolvidas atribuídas à GamerCard após a sobreposição), sem alterar seus
hints. Sign-in passou a usar `signin/signin_17559_manifest.toml`, `is_dll=true`
e registro explícito da DLL nativa no mesmo runtime. Seu diretório separado
impede também que o codegen sobrescreva o CMake gerado da dashboard.
A geração final da dashboard preservou seus 428 arquivos de saída sem alterações.
Ajustados somente cinco pontos de entrada confirmados do Sign-in, ausentes da
.pdata: restante de memcpy e quatro thunks de vtable.

Patch reproduzível 0040, com pin inalterado: aceita ANY_USER FF, conserva entrada
exclusiva e implementa o ABI `(user, flags, output)` de HudEx (`816F4C78`).
Consulta no máximo quatro controles por chamada, sem espera; leituras de estado
com o bit de sistema continuam chegando ao dispositivo. Novos imports XUI do
Sign-in são encaminhados às funções do XAM 17559 já recompiladas.

Validação focada: `x360n_sdk_runtime_checks.exe "[signin]" --reporter compact`,
**3 testes / 76 asserções aprovadas**, incluindo entrega sintética de B, estado
neutro para o título versus estado real para o HUD, argumentos inválidos,
fechamento diferido, reabertura e propagação de falha/OOM sem notificações.
Log: `.local/tmp/c71-tests.log`. Patch 0040 reaplicado em cópias dos dois arquivos
base, com hashes finais idênticos; `apply-sdk-patches.ps1 -Check` aprova os 40.
Build final via `scripts/devshell.cmd cmake --build .local/build/titles/dash_17559
--target dash_17559 --parallel 8`: saída 0, com DLL Sign-in e host ligados.
Log: `.local/tmp/c71-build-title.log`. O cabeçalho pré-compilado do alvo ficou
obsoleto durante o ajuste da geração; somente esse artefato foi removido e
reconstruído com o componente. Nenhum build completo do projeto foi executado.
Não foram executados Computer Use, jogos, testes visuais ou suítes completas.

**Não comprovado em execução:** criação da cena, pixels apresentados, navegação,
B fechando o aplicativo original e retorno de foco. O teste de entrada comprova
o transporte, não o consumo pelo código do Sign-in. C7/GamerCard conserva as
três aberturas/fechamentos e 107 asserções anteriores, sem reexecução.

Bloqueio conhecido fora do incremento: serviços de perfis ainda incompletos
(`XamProfileCreateEnumerator`, `XamProfileEnumerate`, logon e tarefas associadas).
O ordinal XAM 054B, importado em `9011DBEC`, não consta da tabela do SDK; sua
implementação original `816DEF20` acessa o serviço de perfis. Há um trap explícito
com LR se ele for alcançado, sem booleano de sucesso inventado. Ainda não há
evidência de que esse ordinal seja atingido antes da primeira apresentação.
Não foram implementados criação/persistência, Live ou Guide.

Verificação manual, em PowerShell na raiz, após o build do componente:

```powershell
& .\.local\build\titles\dash_17559\dash_17559.exe --gpu_plugin=xenos --mnk_mode=true --game_data_root="$PWD\.local\inputs\dashboards\17559\runtime" --dash17559_boot_intro=false --dash17559_signin_ui=true
```

A opção é desligada por padrão e envia **uma única** solicitação através do thunk
original da dashboard `92192AE8`, no primeiro quadro da dashboard. Ela permite
validar a integração sem depender do caminho Settings/Profile (GamerCard) ou
alterar o usuário fixo do SDK. Conferir a interface original, mover o foco e usar
B (Backspace no mapeamento MnK); depois conferir o retorno do foco à dashboard.
Fechar a janela se houver falha. Os logs distinguem initialize, create scene,
attach/layout, composed frame e closed; retorno 0 de uma API não comprova pixels.

Para uma preparação de desenvolvimento nova, gerar primeiro
`titles/dash_17559/signin/signin_17559_manifest.toml` com `rexglue codegen`,
depois o manifesto principal, e usar somente o alvo `dash_17559` no build.
O CMake recusa explicitamente a ausência dessa geração; a distribuição publicada
não foi alterada. Nenhum binário ou código derivado do XEX foi incluído no Git.

### 0.26 Perfil local: backend persistente C7.2 (2026-10-13, parcial)

O usuário confirmou apresentação, entrada e fechamento do Sign-in e GamerCard
originais. Não foram repetidas essas validações. O critério funcional de C7
**permanece pendente**: não existe ainda criação/seleção utilizável pela cena.

Patch 0041 no mesmo SDK/kernel, sem launcher ou UI nova:

- `UserProfile::CreateLocal(nome)` cria um único registro local desconectado,
  com XUID aleatório gerado uma vez, prefixo local E000 e nome ASCII de 1–15
  caracteres (sem espaços nas extremidades). Não cria automaticamente “User”.
- `SetLocalSession(xuid)` entra nesse registro; zero desconecta, preservando
  identidade. XUID desconhecido e segundo perfil são rejeitados. Não há catálogo
  de múltiplos perfis, exclusão ou migração de saves.
- `KernelState` recupera o registro na inicialização. As consultas XAM de estado,
  XUID, nome, gamertag e SigninInfo usam esse serviço; perfil desconectado não é
  anunciado como usuário conectado. Perfil local não anuncia Live/Gold nem os
  250 pontos fictícios anteriores.
- `XamUserLogon`/`Ex` deixam de ser stubs no subconjunto local flags=0. O primeiro
  recebe quatro XUIDs; Ex recebe quatro entradas de 392 bytes. Só slot 0 pode ser
  preenchido. Modos não implementados retornam E_NOTIMPL antes de alterar estado.
  Confirmações repetidas não geram SIGNINCHANGED; inicialização de listener deixou
  de fabricar duas mudanças de usuário. Não há leitura de disco por quadro.

Persistência: `<user_data_root>/profiles/local-v1.dat`, registro binário de 33
bytes (`X360LP01`, XUID big-endian, byte de sessão, nome terminado em zero).
Grava arquivo temporário na mesma pasta, faz flush e substitui o registro antes
de publicar o estado/notificar. Windows usa `_commit` e `MoveFileExW` com
REPLACE_EXISTING/WRITE_THROUGH. Arquivo inválido é preservado, e a sessão permanece
desconectada; não há recuperação silenciosa que sobrescreva dados. A implementação
supõe um runtime escritor por pasta. O caminho segue a configuração existente;
sem `user_data_root` explícito, o runtime usa `game_data_root`. Unificar pastas
de dados entre títulos e integrar saves por usuário continuam fora deste passo.

Evidência estática do ABI, obtida dos arquivos locais já fornecidos: Sign-in
`90107440`/`90107450` encaminham as chamadas aos imports Logon/LogonEx; XAM
`8199C630` expande quatro XUIDs para entradas de 392 bytes, `8199EEC0` substitui
os slots no modo flags=0 e `8199EF14` trata a entrada zero. O backend usa os
exports compartilhados já ligados à DLL AOT. Não há hook de botão ou confirmação
artificial no host XUI. Isso **não demonstra** que uma seleção na cena percorreu
o novo serviço.

Bloqueio preservado para o próximo incremento: `XamProfileCreateEnumerator`
(`816DB860`) e `XamProfileEnumerate` (`816D7860`) dependem de registros de conta;
`XamProfileCreate` (`816DC468`) cria uma tarefa assíncrona com payload de conta de
380 bytes e saída de tarefa. Implementar somente um retorno zero não produziria
um perfil. Faltam esses contratos, consulta/conclusão de tarefas e a identificação
do modo de logon efetivamente escolhido pela cena. Ordinal 054B mantém o trap.
Criação pelo serviço C++ serve aos testes; não foi exposta como alternativa de UI.
Cancelamento da cena não foi exercitado neste incremento. O backend só muda com
uma chamada explícita de sessão, nunca ao abrir/fechar a UI.

Não executados: Computer Use, dashboard, jogos ou suítes completas. Testes
sintéticos focados cobrem serviço + exports compartilhados e reinicialização do
runtime, sem elevar o aceite visual/funcional do Sign-in original.

Validação: **3/3 `sdkcheck.Local profile` aprovados**, 0,95 s via CTest com timeout
de 30 s por caso; log `.local/tmp/c72-tests.log`. A fixture usa pasta isolada no
build: a tentativa em `%TEMP%` expôs `MoveFileExW` negado (erro 5) neste ambiente,
sem publicar estado. Build de `x360n_sdk_runtime_checks` concluído, saída 0
(`.local/tmp/c72-build-final.log`). Patch 0041 revertido/reaplicado em cópias dos
quatro arquivos com os mesmos atributos LF do SDK, hashes base/finais conferidos.
O alvo `dash_17559`, incluindo `dash_17559_signin.dll`, também compilou com saída
0 (`.local/tmp/c72-build-title.log`), sem execução gráfica. Os 41 patches passaram
em `apply-sdk-patches.ps1 -Check` (`.local/tmp/c72-patches.log`).

### 0.27 Enumeração real no Sign-in original: C7.3 (2026-10-13, parcial)

Base: `468fc66`. Patch 0042 reutiliza UserProfile, KernelState, host XUI,
DLL AOT e `profiles/local-v1.dat` do incremento anterior. Nenhuma tela,
backend, formato de persistência ou perfil de produção foi criado.

Contratos acrescentados e evidência estática dos binários locais 17559:

- `XamProfileCreateEnumerator(0, handle)` e `XamProfileEnumerate(handle, 2,
  record, 0)` atendem a sequência original `90118C3C–90118E98` via vtable
  `901014A8`, thunks `90107920/90107930`. Cada enumerador de kernel tem seu
  cursor e snapshot. Fim retorna NO_MORE_FILES; NtClose libera o handle.
- O registro tem XUID local em +0, conta de 380 bytes em +8 e dispositivo em
  +388. Nome UTF-16 em conta+8, XUID Live em conta+40 zerado. O único registro
  vem do serviço persistido, mesmo quando está desconectado. Sem perfil, a
  enumeração é vazia. O store não é anunciado como dispositivo removível.
- FindAccount/LoadAccountInfo consultam essa identidade. Três adaptadores raw
  preservam os 64 bits do XUID: o marshaler genérico do SDK fixado retorna
  somente `ctx.rN.u32`, detectado pelos testes novos antes da correção.
- A confirmação normal em `901186BC–90118720` usa LogonEx `0x120420`.
  Original XAM `8199E884` segue `8199EEC0` quando não há bits 4/8, substituindo
  os slots. Esse modo local passa pela transação existente, aceita apenas
  slot zero e perfil conhecido, rejeita conta Live/modos não implementados.
  Não copia a identidade fornecida pelo chamador sobre a persistência.
- GetUserIndexMask, GetIndexFromXUID e GetDeviceId consultam o estado
  compartilhado; não há outro kernel nem leitura de disco por quadro.
- O log da execução revelou XMemCpy, XMemSet e lstrcpynW como stubs usados
  pela cena. Os dois primeiros agora usam o CRT existente; lstrcpynW atende
  cópia limitada/terminação UTF-16 conforme `81722F00`. Essas correções
  posteriores à captura receberam teste unitário, sem segunda execução gráfica.

**Execução única:** `.local/evidence/runs/c73-signin-profile/rex.log` registra a chamada
real de enumeração pelo original e `1 record(s)` às 17:09:48.178. A captura
`signin.png`, feita pelo script local `capture-window.ps1`, mostra `C73Local`
na interface original. Foi usada uma fixture desconectada de 33 bytes em
`.local/tmp/c73-dashboard-data/profiles/local-v1.dat` (XUID E00000000000C730),
separada dos dados de uso. Esse nome veio da conta enumerada, não de texto
substituído na cena. A sessão ficou desconectada e o arquivo não mudou.
Processo PID 33708 encerrado pelo limite de aproximadamente 30 s; nenhum
processo de dashboard ficou aberto. Não foram enviados cliques/teclas por
scripts nem usado Computer Use.

A primeira tentativa pelo supervisor foi recusada antes de lançar o título:
“Preparação inválida; prepare novamente. A preparação anterior não terminou.”
A única execução efetiva foi direta sobre o build de desenvolvimento, com
WaitForExit limitado e Stop-Process em finally. Não se refez preparação/codegen
para contornar recibos. Os comandos de build consultaram os carimbos existentes;
seus resumos indicam zero arquivos de código gerado escritos.

**Limite de aceite:** essa execução não confirmou o perfil pelo botão. Não
comprova a sequência seleção → login → dashboard/GamerCard, nem cancelamento
na cena. Os testes comprovam a fronteira de serviço (enumerar/fechar sem mudar,
confirmar via export muda e persiste, consultas compartilham a identidade),
não o tratamento do botão original. C7 continua parcial. As tarefas XAM e
outros modos de logon, criação pela UI e ordinal 054B ainda estão incompletos;
não foram substituídos por sucesso fictício. Não foi demonstrado que qualquer
um deles bloqueia especificamente a confirmação normal nesta execução.

Validação final focada: **5/5 `sdkcheck.Local profile`**, 1,27 s, timeout 30 s
por teste (`.local/tmp/c73-tests.log`), incluindo os testes anteriores de
persistência/falha e os novos contratos de enumeração, confirmação local,
XUID, consultas, cursores, fechamento e cópia dos dados originais. Builds
`x360n_sdk_runtime_checks` e `dash_17559` com seus componentes ligados, sem
suíte completa ou regressões antigas. Logs `c73-build-tests.log` e
`c73-build-title-final.log` em `.local/tmp`. Patch revertido/reaplicado em cópias
isoladas: os três hashes base e final coincidem com o lock. Os 42 patches
passam `apply-sdk-patches.ps1 -Check` (`c73-patches.log`). SDK pin preservado,
sem push, sem alterar a Release publicada.

### 0.28 Confirmação original: C7.4 (2026-10-13, bloqueado)

Base `3d6045c`; nenhuma alteração funcional neste incremento. O objetivo
seleção → login real → identidade da dashboard **não foi alcançado**.

Uma única execução gráfica de desenvolvimento, autorizada para entrada manual,
durou 58,153 s (PID 30384, encerrado pelo script; nenhum processo remanescente).
Computer Use e envio de entrada por scripts não foram utilizados. Dados isolados
em `.local/tmp/c74-dashboard-data/profiles/local-v1.dat`: perfil existente
`C73Local`, XUID `E00000000000C730`, sessão inicialmente zero. Os arquivos
`before.dat` e `after.dat` em `.local/evidence/runs/c74-original-confirm/` têm o mesmo
SHA-256 `B3199E283D5AAB0A6F6DC8DFC4543752812B5AF485D711527C12478ADE1675FA`;
o byte de sessão permaneceu zero. O usuário informou: “O Sign-in apareceu,
mas A não entrou” e “Pressionei B, mas não fechou”. Cancelamento sem mudança
de arquivo não equivale a cancelamento funcional: a interface não fechou.

**Bloqueio observado e contrato ausente:** as capturas locais `state-20.png`,
`state-35.png` e `state-50.png` preservam a sessão; as de 20 e 50 s mostram o
perfil com cadeado e o modal “Enter Pass Code / Enter your Xbox Live pass code”.
Às 17:25:30.437 o log registra `XamShowMessageBoxUI(FF, 40070DF0, 40070D80,
0, 0, 0, 00020003, 7015F0F8, 7015F100)`. Em
`external/rexglue-sdk/src/kernel/xam/xam_ui.cpp`, esse export só interpreta
`flags & 0xF` para ícone e cria `MessageBoxDialog`. Não implementa a entrada
especial solicitada por esse fluxo. `OnDraw` fecha por botões ImGui; aqui a
lista está vazia, não há tratamento de B, e o popup não oferece fechamento.
`xeXamDispatchDialog` aguarda a fence de fechamento antes de concluir o
overlapped. A linha “running completion” às 17:25:30.537 indica entrada nessa
espera, não conclusão bem-sucedida do pedido.

Rastreamento estático do original local 17559, sem executar callbacks à parte:

- Na dashboard, `928B4220` consulta o item selecionado (XUID/dispositivo),
  verifica seu método virtual `+20` e, quando verdadeiro, copia a conta pelo
  método `+52` e chama `92189268`. Este monta quatro símbolos a partir de
  conta+56, solicita os textos 160/161, chama `XamShowMessageBoxUI` em
  `92189384` com flags `0x00020003` e espera o overlapped em `92189398–921893C0`.
  É o contrato compatível com a chamada e o modal observados. **A causa de o
  perfil local entrar no ramo de pass code ainda não foi determinada**;
  não se concluiu que a conta tenha senha configurada nem se alteraram flags
  para forçar a passagem.
- Na DLL `signin.xex`, a rota normal encontrada foi `90115EE8` (guarda
  XUID/dispositivo selecionados) → tarefa/callback `901159C8` → método de
  login `901183E0` → `XamUserLogonEx` em `90118720`. Isso é evidência estática,
  não demonstra que A tenha executado essa tarefa na sessão acima.
- A rota do host para essa DLL falhou antes: `original create scene ->
  80070525, root=00010057` às 17:25:23.396, seguida de desmontagem e resultado
  FE/21001=1627, estado=0. A tela visível da dashboard não comprova sucesso
  desse host. A origem exata desse HRESULT permanece pendente; tarefas XAM
  incompletas foram encontradas, mas não demonstradas como sua causa.

Continuar pelo modal exigiria tratar o contrato de entrada, cancelamento e
conclusão da UI XAM original, além de esclarecer o ramo de pass code; corrigir
somente o serviço de login não desbloqueia a espera. A investigação foi
interrompida nessa dependência, conforme o limite de escopo. Não foi acrescentado
outro serviço, UI substituta, conclusão automática, notificação de login ou
infraestrutura de perfis. Dashboard/GamerCard com o XUID conectado **não
demonstrados**. A e B **reprovados na sessão manual**.

Evidências privadas em `.local/evidence/runs/c74-original-confirm/`: capturas por script
local, cópias do perfil, `binaries.txt` com hashes dos três binários utilizados e
`rex.log` SHA-256
`6FD8E1DD4D4FCFB694D5D2884CBB7D8A944C6263AB18EA8720A5B61647E5D40E`.
Não foram executados testes unitários/backend, builds, suítes completas, jogos
ou outra dashboard: nenhuma implementação mudou. Somente documentação de
resultado/bloqueio foi alterada; Release publicada preservada, sem push.

### 0.29 Falso passcode corrigido: C7.5 (2026-10-13, parcial)

Base `b445001`; HEAD local preservado. **O objetivo de login pela interface
original não foi alcançado.** Há uma correção funcional comprovada do bloqueio
anterior, mas a única execução autorizada encontrou outro bloqueio. Nenhum
login artificial, chamada de confirmação pelo host ou edição de sessão foi feita.

**Causa determinada do “Enter Pass Code”:**

- A confirmação da dashboard em `928B4A88` chama `928B4220` em `928B4B08`.
  O método virtual +20 do item selecionado decide se deve chamar `92189268`,
  responsável pelo MessageBox com flags `0x00020003` e textos 160/161.
- A vtable `9213F8CC`, observada na imagem original local, aponta +20 para
  `92925498` e +52 para `92925508`. O primeiro lê o booleano do item em +704;
  o segundo copia os 380 bytes da conta, armazenados em item+24.
- O construtor `92925530` copia a conta enumerada. Em `92925650–92925674`,
  chama `RtlCompareMemoryUlong(item+80, 4, 0)` e grava em item+704 se o
  resultado **difere de 4**. Item+80 é conta+56, os quatro bytes de passcode.
  Conta+40 (XUID Live) determina outro booleano, em item+700, não esse teste.
- `AccountFor` do patch 0042 já entrega os quatro bytes zerados, o XUID Live
  zerado e os campos iniciais de controle zerados. O perfil persistido não tem
  senha configurada. O defeito estava em `xboxkrnl_rtl.cpp`: a função retornava
  **uma palavra** correspondente, e não **quatro bytes**. Isso explica tanto
  o cadeado como a entrada indevida no ramo do modal. Não foi necessário
  mudar flags da conta, armazenar uma senha nem implementar teclado de passcode.

**Contrato corrigido:** patch reproduzível 0043, registrado em `deps.lock.json`,
retorna o comprimento em bytes do prefixo de ULONGs iguais, parando na primeira
palavra diferente. A ordem big-endian já era tratada por `as_array`; foi
preservada, assim como a rejeição de endereço/comprimento desalinhados. A
mudança não toca `RtlCompareMemory`, serviços de conta ou outros modos de login.
O pin `f5337cdc947ff6d4c4196737e2c807a48f2a1fc2` permanece.

**Validação focada:** `sdkcheck.Local profile passcode comparison returns
matching bytes in guest order` falhou antes da correção (5/9 asserções; quatro
zeros retornavam 1). Depois, esse teste e `sdkcheck.Local profile enumerated
account has no passcode without changing session` passaram: **2/2**, 0,70 s
CTest, timeout 30 s por caso. O segundo exercita a conta realmente enumerada
pelo runtime, seu XUID, controle/Live/passcode zerados, o export corrigido e a
sessão desconectada preservada. Logs privados em `.local/tmp/c75-test-before.log`
e `c75-tests-final.log`. Builds incrementais dos alvos `x360n_sdk_runtime_checks`
e `dash_17559` concluídos com saída 0 (`c75-build-focused.log`,
`c75-build-title.log`); codegen do título reportou zero arquivos escritos.
O primeiro build do teste acusou include de `<array>` ausente, corrigido antes
de executar o teste; não foram usados binários antigos após essa falha.
Reverse/apply em cópia isolada conferiu hashes base/final (`c75-replay.log`).
Os 43 patches passaram `apply-sdk-patches.ps1 -Check`. Não foram repetidas
regressões anteriores, testes dos jogos ou suítes completas.

**Única execução gráfica:** `.local/evidence/runs/c75-original-confirm/`, PID 29820,
56,114 s, encerramento pelo script `.local/tmp/c75-run.ps1`, sem processos
remanescentes. Entrada manual do usuário, capturas reais por
`scripts/diagnostics/capture-window.ps1`; nenhum Computer Use ou entrada por script.
Dados isolados copiados da fixture existente, nome `C73Local`, XUID
`E00000000000C730`. `state-8.png` mostra o perfil sem cadeado. `state-35.png`
e `state-50.png` mostram o corpo vazio do Sign-in. Não há chamada a
`XamShowMessageBoxUI` no log. O usuário relatou: “entrou em uma tela vazia e b
nao voltava”. **A não concluiu login; B não cancelou corretamente.**
As cópias de perfil em 8/20/35/50 s e ao final mantêm o byte de sessão zero.
`before.dat` e `after.dat` têm SHA-256
`B3199E283D5AAB0A6F6DC8DFC4543752812B5AF485D711527C12478ADE1675FA`.
Logo, não há evidência de XUID conectado nem identidade conectada na dashboard;
preservar um arquivo desconectado não aprova cancelamento de sessão conectada.
Hashes dos binários estão em `binaries.txt`; `rex.log` tem SHA-256
`60664355D973F644BCF07511B30E9EA1EA5B04B41F9C6DDD4E28980F3F8B87D2`.

**Bloqueio restante e limites da causa:** às 18:19:26.406 (horário do log),
o host ainda retorna `original create scene -> 80070525, root=00010057`,
seguido de desmontagem e FE/21001=1627. `80070525` corresponde a
HRESULT_FROM_WIN32(NO_SUCH_USER/1317). Ocorre antes de pressionar A e permanece
após corrigir a comparação; não é prova de senha incorreta. **A chamada que
origina esse HRESULT não foi determinada nesta investigação**; não atribuir
causalidade a um import específico sem evidência. A interface visível é a rota
da dashboard, não uma criação bem-sucedida desse host `signin.xex`.

A continuação estática imediatamente após a decisão de passcode é
`928B4B14–928B4B58`: prepara estado e agenda o callback `928B4958` por método
virtual. Esse callback chama o método de login em `928B49B4` e publica estado
6 somente em `928B4A40–928B4A64`. Não há rastreamento dinâmico suficiente para
atribuir a tela vazia a uma dessas operações. O log contém consultas repetidas
FC/58003 não implementadas e cancelamento/espera de tarefa incompletos durante
a desmontagem do host; **isso não demonstra que sejam a causa da tela vazia**.
Não foram preenchidos esses serviços com sucesso fictício, substituído o
callback por login direto, nem ampliada a implementação de tarefas/XUI sem
causa demonstrada. O limite gráfico foi consumido. Próxima investigação deve
instrumentar somente esse callback e a propagação de erro em `90106F80`, antes
de qualquer nova sessão autorizada. C7.5 continua parcial, sem push ou Release.

### 0.30 Sign-in local completo para perfil existente: C7.6 (2026-10-13)

Base `15a0251`, SDK `f5337cdc947ff6d4c4196737e2c807a48f2a1fc2` preservado.
**A seleção original agora conecta o perfil existente e retorna à Home.**
GamerCard mostra a identidade conectada, B cancela/fecha e o reinício recupera
a mesma sessão. Alcance comprovado: Retail 17559, Windows x64, um perfil local
persistido no slot 0. Criação pela interface, múltiplas contas e Xbox Live não
fazem parte deste resultado; o perfil não é apresentado como conectado ao Live.

As causas e os contratos corrigidos são:

- `signin.xex`: `90106F80 → 90114EA8 → 901073D0` consultava `GetXUID(0,1)`
  pelo ramo de data center. `XamIsDataCenterMode` era stub e devolvia o ponteiro
  recebido em r3 como verdadeiro. Modo retail seleciona a enumeração normal;
  `GetXUID` continua retornando NO_SUCH_USER para slot desconectado.
- LogonEx de confirmação usa `0x120424`, registros de 392 bytes e XUID de
  64 bits. A seleção passa pelo armazenamento transacional existente; falhas
  de disco não publicam sessão/notificação. Snapshot de fechamento `0x63`,
  índices, flags locais e barreira overlapped usam o mesmo estado.
- Tarefas XAM executam o callback original com contexto opaco e handles reais.
  Status pendente/completo segue `S_OK/S_FALSE`; resultado vem do callback.
  Cancelamento e espera aguardam o worker antes de liberar contexto/módulo.
- O host executa a UI em contexto de processo system, carrega a tabela original
  de strings e encaminha o diálogo original. Entrada HUD respeita a cena/modal
  ativa. As decisões do diálogo são persistidas em XConfig, não pré-aprovadas.
- `XamIsNuiAutomationEnabled` reproduz o falso da função retail `81964638`;
  o stub anterior desviava a atualização normal das transições da dashboard.
  A consulta de demanda inicializa os outputs e informa ausência de demanda
  de autenticação online no runtime local.
- **Causa final da tela vazia após A:** a dashboard enviava `XNotifyBroadcast`
  com ID `0x80040016`, dado 0; o SDK descartava a chamada. A entrega real aos
  listeners permite que a própria dashboard feche a seleção e navegue à Home.
  Não há salto forçado, timer de fechamento ou substituição da interface.
- As consultas internas da biblioteca XUI/GamerCard usam a identidade XAM
  persistida, assim como os imports externos da dashboard e do Sign-in.

Validação incremental: 14 casos focados distintos aprovados (7 de perfil,
6 de Sign-in e o novo broadcast). A consulta de demanda e o broadcast receberam
reteste após suas mudanças; não foi executada suíte completa nem outro jogo.
Builds da dashboard/DLLs e do alvo `x360n_sdk_runtime_checks` concluídos.
Os 53 patches foram reaplicados sobre fontes da revisão pinada, com hashes
base/finais conferidos (base histórica SDL usa CRLF; SDK usa LF), e `-Check`
passou. Evidências de build/teste/replay em `.local/tmp/c76-*`.

Execuções com entrada por mensagens locais e PNGs reais por PrintWindow:

| Diretório em `.local/evidence/runs/` | Evidência |
|---|---|
| `c76-notify-native` | Seleção normal da dashboard: perfil desconectado → A → sessão 1 → Home. |
| `c76-accept-hosted` | `signin.xex` original lista C73Local; A, diálogo original/Yes, login, Home, GamerCard com C73Local e B voltando a Settings (40,849 s). |
| `c76-accept-cancel` | B fecha o host original e devolve à seleção de boot subjacente; registro byte a byte inalterado (17,390 s). |
| `c76-accept-cancel-native` | B na seleção normal de boot retorna à Home; registro desconectado inalterado (17,341 s). |
| `c76-accept-restart` | Novo processo, sem flag de diagnóstico: Home, GamerCard com C73Local e B; sessão 1 preservada (25,965 s). |

`before.dat`/capturas seguintes documentam o registro; nenhum teste altera
diretamente seu byte de sessão. O backend só grava em resposta ao fluxo guest.
`rex.log`, `keys.txt`, `result.txt`, `run.txt` e `binaries.txt` acompanham as
capturas. Os testes usaram cópias isoladas do perfil existente. O teste hosted
também persistiu a resposta de privacidade pelo diálogo original.

Reprodução autorizada com `scripts/validation/test-signin-17559.ps1 -UserDataRoot <cópia>
-Out <evidência> -Mode Confirm -GamerCard`; `-Hosted -ConfirmPrivacy` exercita
o módulo Sign-in sobre a dashboard e seu diálogo de primeiro uso. `-Mode Cancel`
requer perfil desconectado; `-Mode Restart -GamerCard` reutiliza o registro
conectado pelo teste anterior. O script limita cada processo a 60 s, encerra em
`finally` e valida persistência; a navegação/conteúdo são conferidos nos PNGs.
Nenhum Computer Use, processo remanescente, push ou publicação.

### 0.31 Múltiplos perfis locais pela interface original: C7.7 (2026-10-13)

Base local `c132d125d6938b98f968aecdef2e89d59b097ed1`; SDK permanece em
`f5337cdc947ff6d4c4196737e2c807a48f2a1fc2`. **Aceite demonstrado na Retail
17559/Windows x64:** criar dois perfis pela interface original, listar, entrar,
alternar, mostrar a identidade correta na GamerCard, sair e recuperar ambos
após reiniciar. As contas são locais, com uma ativa no slot 0.

O fluxo usa `hud.xex`, `signin.xex`, `createprofile.xex`, `vk.xex` e
`gamerprofile.xex` originais recompilados, com o host XUI existente. Os nomes
Player1/Player2 foram sugeridos pelo próprio CreateProfile e confirmados no
teclado original. Não houve criação C++ de fixture, edição do arquivo de
perfil, substituição de telas, salto de callback ou login forçado pelo host.
Guide abre com o botão correspondente; no teclado, G é o padrão quando não
há vínculo configurado. X abre Sign-in/Sign-out, A confirma e B volta.

**Implementação e contratos:**

- Patches 0054–0066, aplicados pelo mecanismo existente e registrados em
  `deps.lock.json`. A enumeração retorna todas as identidades; consultas de
  conta resolvem pelo XUID, independentemente da seleção atual. Criação valida
  o nome e produz um XUID estável, sem conectar a conta por conta própria.
- `XamProfileCreate` recebe a conta guest e produz um handle real de operação;
  `XamProfileGetCreationStatus` devolve resultado/XUID e consome esse handle.
  O CreateProfile original confirma com Logon `0x24` (array de XUIDs); o
  Sign-in usa LogonEx `0x120424`; Guide remove a identidade ativa com `0x28`.
  Resultados overlapped e notificações refletem a operação efetiva.
- O primeiro perfil conserva `profiles/local-v1.dat` (33 bytes). Ao criar o
  segundo, a gravação migra atomicamente para `local-v2.dat`: magic `X360LP02`,
  quantidade BE32, XUID selecionado BE64, registros de 25 bytes com XUID,
  estado e nome. O v1 fica preservado como origem da migração. V2 inválido
  não provoca fallback silencioso nem substituição. Limite: 64 perfis,
  nomes ASCII de 1–15 caracteres, duplicatas sem distinção de caixa rejeitadas.
  Falhas de gravação não alteram identidade/sessão publicada. A troca invalida
  o cache binário específico de título da identidade anterior.
- O host copia pedidos entre aplicativos e descarrega a cena anterior antes
  de carregar outra imagem na mesma base. Após selecionar armazenamento,
  executa o ciclo original de serialização, destruição, restauração, criação
  e layout. O teclado compartilha recursos com CreateProfile sem substituir
  o callback do aplicativo. O registro original de pacotes informa a ausência
  de AvatarMiniCreator; o código original apresenta a página Save Profile.
- O Guide revelou um erro de memória: `XamNuiGetDeviceStatus` escrevia 24 bytes
  em um resultado original de 16, sobrescrevendo r29 salvo em `913F80F8`.
  Patch 0064 limita a estrutura ao tamanho original; o teste com sentinelas
  falhou antes e passou depois. O callback de Sign-out `913E9200`, fora da
  tabela pdata, foi incluído na análise AOT do HUD.
- Sign-in aberto pelo Guide pertence ao controlador 0, mas habilita e consulta
  entrada com ANY_USER (`FF`). Patches 0065/0066 aceitam esse pedido mantendo
  a exclusividade do dono; não consomem eventos de outro controle. O teste
  com driver de teclado real detectou o bloqueio antes da correção.
- `XamLoaderGetGameInfo` continua sem provedor no host local e agora informa
  `E_NOTIMPL` explicitamente neste título. O Guide segue seu ramo original de
  ausência de informação; o antigo stub deixava r3 como falso sucesso e
  tentava decodificar uma imagem em endereço zero. Isso não implementa game-info.

**Evidência real do aceite**, sob `.local/evidence/runs/`, com armazenamento inicialmente
vazio `.local/tmp/multi-final66-data`. Todas estas execuções usaram o botão Guide
normal, sem a flag de diagnóstico `dash17559_signin_ui`:

| Diretório | Duração | Resultado e captura conferida |
|---|---:|---|
| `multi-final66-create1-guide` | 34,267 s | Criação de Player1 pelo teclado e Save Profile originais; conclusão conecta a conta e fecha a cena. `step-2.png` mostra o teclado. |
| `multi-final66-create2-guide` | 52,424 s | Sign-out de Player1, criação de Player2 e Done. `step-11.png` mostra Save Profile/Player2; `step-12.png`, Home. |
| `multi-final66-player2-card` | 30,803 s | Reinício conectado em Player2; `step-4.png` mostra a GamerCard com Player2, B retorna a Settings. |
| `multi-final66-switch1` | 42,469 s | Sign-out de Player2, seleção de Player1, confirmação do diálogo original de privacidade e retorno à Home. |
| `multi-final66-player1-card-signout` | 51,385 s | `step-4.png`: GamerCard Player1. Depois B, Guide, Sign-out/Yes. `step-12.png`: ambos listados, ambos desconectados no snapshot. |
| `multi-final66-restart-signedout` | 18,938 s | Novo processo recupera os dois perfis desconectados; `step-1.png` mostra ambos no Sign-in original. |

XUIDs conferidos antes/depois dos reinícios:

| Nome | XUID | Estado após Sign-out e último reinício |
|---|---|---|
| Player1 | `E00E0FF7A26F67C3` | Desconectado |
| Player2 | `E00D0506D8328811` | Desconectado |

Cada diretório contém `rex.log`, `keys.txt`, `run.txt`, `result.txt`,
`binaries.txt` (SHA-256 do executável e DLLs), PNGs e snapshots JSON/DAT por
passo. O script lê os registros para verificar o efeito; todas as gravações
são consequência da UI guest. A execução adicional
`multi-acceptance-switch-same-session` (48,945 s, outro armazenamento isolado)
comprova no mesmo processo: desconectado → Player1 → Sign-out → Player2.
O log registra os três pedidos reais e os snapshots mostram as transições.

As tentativas intermediárias permanecem como diagnóstico. Em particular,
`multi-acceptance-signout` persistiu a saída, mas encontrou o callback AOT
ausente; seu resultado foi corrigido para FAIL e o detector de falhas passou
a incluir `[FATAL]`/`Indirect failure`. Não é contado como aceite. Algumas
capturas PrintWindow durante transições ficaram pretas; não são usadas como
prova visual. Os PNGs identificados na tabela acima foram inspecionados.

**Builds e testes:** builds incrementais de `dash_17559` e
`x360n_sdk_runtime_checks` concluídos. Dezessete casos focados distintos
aprovados ao longo dos incrementos: identidade/sessão/reinício, enumeração e
conta, passcode, criação/handles, duplicatas, migração/falhas/corrupção,
overlapped, pedidos originais, isolamento/resume de UI, entrada ANY_USER e
limite de memória do NUI. Logs em `.local/tmp/`:
`multi-tests.log`, `multi-test-overlapped-retry.log`,
`multi-create-service-test-fixed.log`, `multi-migration-test.log`,
`multi-filter-test.log`, `multi-queries-test.log`, `multi-ui-requests-test.log`,
`multi-resume-test.log`, `multi-logon-tests-fixed.log`, `multi-guide-tests.log`,
`multi-nui-after.log`, `multi-enable-tests.log`.
O primeiro lote teve uma falha de substituição de arquivo no Windows; o caso
overlapped passou no reteste focado. Os testes NUI/entrada falharam antes de
seus patches e passaram depois. Não foi executada suíte completa.
Builds finais: `multi-build-enable-title.log` e `multi-build-enable-test.log`.
`multi-replay-66.log` confirma reaplicação dos 66 patches sobre fontes do SDK
pinado, com hashes base e finais iguais aos registrados no lock.

**Reprodução local autorizada:** os novos manifests ficam em
`titles/dash_17559/{createprofile,vk,hud}/`; Sign-in conserva o manifest próprio.
São imagens AOT separadas porque suas bases guest podem se sobrepor.
Para preparar esses fontes gerados antes do configure do alvo de desenvolvimento,
execute `rexglue codegen` em cada um dos quatro manifests, usando a ferramenta
compilada do SDK pinado. Os hashes dos XEX originais e as DLLs necessárias
constam de `titles/dash_17559/preparation.toml`; código gerado e conteúdo
original ficam ignorados. O build incremental existente é:

```powershell
scripts/devshell.cmd cmake --build .local/build/titles/dash_17559 --target dash_17559
```

Com uma pasta de dados vazia já criada, o roteiro do aceite é:

```powershell
$data = '.local/tmp/perfis-novos' # criar vazia; não reutilizar dados pessoais
New-Item -ItemType Directory $data
scripts/validation/test-local-profiles-17559.ps1 -UserDataRoot $data -Out .local/evidence/runs/perfis-create1 -Sequence '71,76,32,13,74,74,32' -ExpectedProfiles Player1 -ExpectedActive Player1
scripts/validation/test-local-profiles-17559.ps1 -UserDataRoot $data -Out .local/evidence/runs/perfis-create2 -Sequence '71,76,85,32,wait:2500,71,76,74,32,13,74,74,32' -ExpectedProfiles Player1,Player2 -ExpectedActive Player2
scripts/validation/test-local-profiles-17559.ps1 -UserDataRoot $data -Out .local/evidence/runs/perfis-card2 -Sequence '51,51,51,72,32,8' -ExpectedProfiles Player1,Player2 -ExpectedActive Player2
scripts/validation/test-local-profiles-17559.ps1 -UserDataRoot $data -Out .local/evidence/runs/perfis-switch1 -Sequence '71,76,85,32,wait:2500,71,76,32,85,32' -ExpectedProfiles Player1,Player2 -ExpectedActive Player1
scripts/validation/test-local-profiles-17559.ps1 -UserDataRoot $data -Out .local/evidence/runs/perfis-card1-signout -Sequence '51,51,51,72,32,8,71,76,85,32,wait:2500,71,76' -ExpectedProfiles Player1,Player2 -ExpectedActive ''
scripts/validation/test-local-profiles-17559.ps1 -UserDataRoot $data -Out .local/evidence/runs/perfis-restart -Sequence '71,76' -ExpectedProfiles Player1,Player2 -ExpectedActive ''
```

O script limita cada processo a 60 s, acompanha sua execução, captura por
PrintWindow e encerra em `finally`. A sequência usa as teclas remapeadas pelo
próprio teste (A=Space, B=Backspace, Start=Enter, X=L, Guide=G, Up=U, Down=J,
Right=H, Left=K, RB=3). Conferir PNGs além das asserções de persistência.
Novas execuções visuais continuam exigindo autorização conforme CLAUDE.md.

Limites: offline, um usuário ativo no slot 0. Xbox Live, cobertura completa
do Guide, personalização de avatar, preferências completas, exclusão/importação,
saves/conquistas e vários jogadores simultâneos não foram aprovados. C7 com
um perfil conserva formato e contratos nos testes; regressões de jogos já
aprovadas não foram repetidas. Sem Computer Use, processos remanescentes,
push ou Release.

### 0.32 Isolamento obrigatório por versão e teclado 17559: C7.8 (2026-10-13)

Base `daa5bf0`. A regra permanente está em `AGENTS.md` e `CLAUDE.md`:
**uma dashboard, seus módulos originais correspondentes; sem mistura de versões**.
Compartilhar infraestrutura genérica ReXGlue continua permitido.

A conferência do arquivo efetivamente usado, `.local/inputs/dashboards/17559/runtime/vk.xex`,
mostrou XEX2 de 45.056 bytes, versão e base **2.0.17559.0**, PE `vk.dll`,
base `917D0000`, entry `917DFBA8`. Os imports XAM e kernel também declaram
2.0.17559.0. SHA-256:
`876ddbb333d83f408ab7c69267703e623daef6da5b1eb3302bd94d246155724e`.
Esse já era o arquivo usado no aceite C7.7; não houve troca para 13604 neste
incremento. **13604 não é substituto permitido**, mesmo com nome/base/exports
iguais. Registro da inspeção: `.local/tmp/version-guard-vk-identity.txt`.

O perfil 17559 associa os seguintes originais, também registrados em
`preparation.toml` e na tabela compilada `src/original_assets.h`:

| Caminho relativo à pasta 17559 | SHA-256 |
|---|---|
| `dash.xex` | `3ad8f38d070eaf63cadac0697e3e8802c565cbfd0825a485347c5d718014f927` |
| `shrdres.xzp` | `179c5be167402297c6e137c08fefe3146f6f497964dc2bda94812596fc9aefe0` |
| `bootanim.xex` | `45eb980c0f841b4807d43c07c766fae5e70b8e2fa4d13fb247a491897cfc6de7` |
| `SegoeXbox-Light.xtt` | `7b03c1731c6cb874adee50b17aa150f6ba3e0671fc8f612a2a970bf5ef4a542e` |
| `XenonJKLatin.xtt` | `9f20409bbe22ca92d483c24aa0930ac8ec761712c16d00c477f145071e707425` |
| `XenonCLatin.xtt` | `977bbe13d5545d300e57dacf9dff492476452f50880ef3868d3aa308d041187d` |
| `XenonSCLatin.xtt` | `6d44d6b65605bee7acfc96bb1d11bafdc3cc3abd57544eca560cd02c84abc590` |
| `system/gamerprofile.xex` | `a99fb2ae20c4b3ffab9fa5f6a73e5766b49fbed5716e8b4d3752d1b97b83aad4` |
| `system/xam_ui.xex` | `78b3dbd8ae866f4258f5444205486ea9f8451afaa30f469c875dcf345f828467` |
| `system/huduiskin.xex` | `fdbe68d20fa6b013282ce8f21cebddc34fe4e3a92a71c28983d63f288d2252cc` |
| `system/signin.xex` | `86695c6c36250cc4b4addb7ccc07d15d54492b352c979cf5c0934804109a6130` |
| `system/createprofile.xex` | `c5dae66e2e7b408ef6d84d659a2232ad89f01d78fa38c365d8732f2e0a221bbd` |
| `vk.xex` | `876ddbb333d83f408ab7c69267703e623daef6da5b1eb3302bd94d246155724e` |
| `system/hud.xex` | `37e10612afde211e1d349838704f7cc2648ad5a4d1f8ca459f18ecd2c1f31ac7` |

Os módulos são do pacote Retail 17559 local; procedência da extração e hashes
permanecem em `versions/17559/retail_system_assets/metadata/`. As fontes não
têm versão XEX: a associação e os bytes do conjunto existente são explícitos
no perfil. SegoeXbox-Light veio do pacote oficial; SCLatin está no conjunto
17559 e no ZIP da atualização. JK/CLatin conservam a procedência do acervo
`resources/fonts/Segoe UI (Kinect, Metro)` já registrada na preparação; não
atribuir a elas uma versão interna não identificada. O runtime não busca
fontes nesse diretório compartilhado: só aceita as cópias locais de hash fixado.

A 6717 mantém `dash.xex` SHA-256
`f8a51ea30406e3b8737e0cdbdad44867743869d59f58d4bc0f4b83e154cb87d8` e
seu `shrdres.xzp` SHA-256
`c411c9f29e6b6031c2e4ce9a3e38e1d3fc9a9ec4d17bf99ba80fb2260a1fb12e`.
`XenonJKLatin.xtt` correspondente segue sem identificação: consta como
dependência não verificada e agora impede build/início, com erro explícito.
Isso preserva a evidência histórica de Blades, mas não permite ocultar a
dependência usando uma fonte de outra versão. Não houve sessão 6717 neste
incremento. 13604 não tem alvo integrado nem fallback para 17559.

**Proteções e custo:** `cmake/runtime/x360n_dashboard_assets.cmake` confere os originais
na configuração e antes dos alvos AOT/build. `VerifyDashboardAssets` verifica
a tabela compilada antes de construir Runtime, inclusive na execução direta
com `--game_data_root`. Arquivo ausente, hash incompatível ou caminho que
resolve fora da pasta resulta em erro com versão, dependência e hash esperado.
O diálogo de erro usa o mecanismo existente do host. Não existe busca por
basename em outras pastas nem fallback. As associações `game:`, `media:` e
`Harddisk0/Partition1` continuam apontando para a pasta da dashboard ativa.

Os hashes são calculados por streaming uma vez por início, usando o SHA-256
existente (BCrypt no Windows), sem trabalho no caminho de renderização/entrada.
Não há cache global de validação por nome/tamanho/mtime. Caches recebem
`dashboard-<versão>`; metadados padrão ficam em `<game_data_root>/metadata`,
desativando a busca no diretório pai do SDK. Metadados explicitamente fornecidos
também recebem o sufixo da versão. Overlays `update_data_root` são recusados
para estes alvos: requerem o pacote completo correspondente. Os originais
devem permanecer imutáveis durante a execução; isto não é uma proteção contra
alteração externa concorrente de arquivos após a verificação.

**Quatro testes focados distintos aprovados:** mistura/ausência/arquivo trocado
com mesmo tamanho e mtime, coerência entre tabela e preparação/caminhos AOT,
hashes reais mais versão/imports do teclado, e rejeições do gate CMake.
Logs: `.local/tmp/version-guard-tests.log`, `version-guard-tests-final.log`,
`version-guard-build-gate-test.log`. Builds incrementais concluídos em
`version-guard-title-final-build.log`, `version-guard-title-metadata-build.log`
e `version-guard-tests-final-build.log`. SDK pinado e patches 0001–0066
inalterados; os 17 testes anteriores continuam como evidência C7.7, sem repetir
automaticamente aquela bateria nem jogos.

**Teclado e perfis reais**, armazenamento novo `.local/tmp/version-guard-profiles`,
somente UI original e scripts locais autorizados, processos limitados a 60 s:

- `version-guard-create-as`: escolher a/s na grade, Done e salvar produziu
  `asPlayer1`, XUID `E006ACB25043B19F` (48,896 s). A asserção esperava `as` e
  falhou: a grade inseriu caracteres antes do nome sugerido. O erro era da
  expectativa do roteiro; `step-7.png` e o snapshot final registram o nome real.
- `version-guard-as-card-signout`: novo processo recuperou `asPlayer1`, GamerCard
  correta em `step-4.png`, B e Sign-out original concluídos (45,852 s, PASS).
- `version-guard-create-qw`: escolher q/w produziu `qwPlayer1`, XUID
  `E00AC037A95DE171`, preservando asPlayer1 desconectado (49,509 s, PASS).
  `step-7.png` mostra o teclado; `step-11.json` confirma as duas contas.
- `version-guard-qw-card-signout`: GamerCard `qwPlayer1` após reiniciar em
  `step-4.png`, seguida de Sign-out real, PASS.
- `version-guard-switch-same-process`: tentativa de roteiro longa, 56,101 s;
  a sequência avançou enquanto o diálogo de primeiro uso ainda estava ativo.
  Terminou conectado em asPlayer1, contrariando a expectativa de qwPlayer1;
  permanece como tentativa reprovada, não como prova da troca completa.
- `version-guard-switch-qw`: 39,428 s, PASS; inicia em asPlayer1, faz Sign-out
  (`0x28`), lista ambos, seleciona qwPlayer1 e conecta (`0x120424`) no mesmo
  processo. Snapshot final: asPlayer1 desconectado, qwPlayer1 conectado.

Os diretórios sob `.local/evidence/runs/` contêm PNGs, log guest, teclas, snapshots e
hashes dos binários. Não houve escrita direta no backend para produzir esses
perfis. Os limites de C7.7 permanecem: offline, um usuário ativo no slot 0,
sem aprovação de saves, Xbox Live, Guide completo ou usuários simultâneos.
