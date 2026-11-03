# Caminhos profundos e armazenamento — 02/11/2026

Base: `main` `8d5a8c4` (v1.28.18 e testes posteriores). Windows 11 Pro x64,
LLVM 22.1.8 fixado, Debug local. Sem alteração de Registro, sem privilégio
administrativo e sem nomes 8.3. Conteúdo original apenas do usuário, fora do
repositório.

**Resumo.** O limite de 141 bytes para preparar no Windows foi retirado.
Uma instalação limpa a 337 caracteres (363 bytes em UTF-8), com Unicode e emoji,
numa unidade NTFS sem nomes 8.3, importou a dashboard 17559 e o Castle Crashers,
preparou os dois e iniciou a dashboard até a tela "sign in or out".
Depois da correção do host de UI (§7), o Castle Crashers iniciou pela My Games
nesse ambiente e chegou à Guide e à seleção de personagem, conduzido pelo
mantenedor; a partida em si não foi confirmada.
A importação de um pacote STFS deixou de copiar os arquivos uma segunda vez:
a escrita caiu de 144,9 MB para 1,1 MB. As ferramentas deixaram de manter os
arquivos baixados depois da extração verificada (961 MB). Gerações sem uso do
SDK compartilhado passam a ser removidas.

## 1. Causa do limite de 141 bytes

O alias `%LOCALAPPDATA%\x360n\r\<id>` (v1.28.18) resolve o diretório de trabalho
dos processos, mas Git, CMake e Ninja resolvem junctions:

- o CMake nomeia seus módulos pelo caminho real, até 101 caracteres abaixo de
  `.local\toolchains`;
- o Ninja recusa nomes acima de 260 bytes em UTF-8;
- o Git abre os metadados do SDK antes de aplicar `core.longpaths`, até 84
  caracteres abaixo de `external`.

Como o alias não encurta o nome real, a v1.28.18 recusava instalações acima de
141 bytes.

## 2. Correção estrutural

**Pastas de dependências curtas (`PlaceDependencyFolders`).** Quando
`<instalação>\.local\toolchains` ou `<instalação>\external` não cabem para essas
ferramentas, a pasta vira uma junction para uma pasta curta do usuário:

- `%LOCALAPPDATA%\x360n\t\<id>` para as ferramentas;
- `%LOCALAPPDATA%\x360n\e\<id>` para o SDK.

Isso acontece antes de qualquer ferramenta rodar, no launcher (antes de
`prepare-tools`) e no pipeline AOT. Uma junction comum de usuário dispensa
privilégio. Caminhos relativos à instalação continuam válidos. A pasta que cabe
fica onde está, e um checkout de desenvolvimento nunca é alterado.

Uma pasta já criada no lugar (instalação movida) é movida para o armazenamento
curto no mesmo volume. Em outro volume, é renomeada para `<nome>.previous-<n>`
e a preparação a preenche de novo.

A recusa só acontece onde nenhuma junction resolve, sempre com a causa:

- compartilhamento de rede;
- sistema de arquivos sem junctions;
- `%LOCALAPPDATA%` longo demais.

Jogos, dashboards, saves, perfis e saídas dos títulos continuam na instalação.

**Caminhos que atravessam a junction (`ToolPath`, `ChildWorkingDirectory`).**
Um arquivo dentro de uma pasta de dependência é passado às ferramentas pelo nome
real curto. Pertencer à instalação passa a ser decidido pelo nome usado, não pelo
nome canônico.

**SDK (patches locais, cada um com teste `sdkcheck` que falha sem ele):**

| Patch | Arquivo | Defeito no ambiente profundo | Teste |
|---|---|---|---|
| 0253 | `codegen/config.cpp`, `project_recompiler.cpp` | `canonical` resolvia o alias; o codegen abria o TOML pelo nome real longo: "File could not be opened for reading" | `Config includes are read through a short junction to a deep folder` |
| 0254 | `core/logging.cpp` | o spdlog abria o `rex.log` longo pelo nome comum e lançava exceção; a dashboard terminava com `0xC0000409` após 957 ms | `A log file beyond MAX_PATH opens in the host form` |
| 0255 | `ui/rex_app.cpp` | `--game_data_root` e o XEX de entrada eram conferidos pelo nome comum; a dashboard parava em "--game_data_root does not exist" atrás de uma caixa modal | `ReXApp starts a guest whose game root is beyond MAX_PATH` |

O lock passa a ter 242 entradas. `apply-sdk-patches.ps1 -Check` confirma todas
aplicadas.

**`prepare-tools.ps1`.** A conferência do SDK exigia o estado final de cada
arquivo e recusava a pilha parcialmente aplicada, mostrando "O SDK precisa ser
atualizado". Agora aceita qualquer prefixo completo da pilha, reconstruído a
partir de `base_files`, como `apply-sdk-patches.ps1` já fazia.

**Build.** `x360n_native_probe` não compilava num build completo desde
`e837aaf`, porque faltava o include do RenderDoc que os alvos vizinhos têm.
A CI de Windows não compila esse alvo. Passou a compilar, e `native.abi` passa.

## 3. Duplicação de arquivos

| Fluxo | Antes | Agora |
|---|---|---|
| Importação de pacote/ISO (extração própria, apagada depois) | cópia de cada arquivo verificado | hardlink da extração para `.local/inputs` quando o volume permite; cópia só se o link falhar |
| Importação de pasta do usuário | cópia | cópia mantida: os originais continuam do usuário e o conteúdo importado é montado gravável |
| System Update 17559 (componentes em staging) | cópia | hardlink do staging (apagado depois); arquivos da pasta do usuário só são lidos |
| Arquivos baixados das ferramentas (`.local/cache/downloads`) | mantidos depois da extração (~961 MB) | removidos quando o recibo `.x360n-archive-sha256` da extração confere com o hash fixado (LLVM: `clang++.exe` presente) |
| Gerações do SDK compartilhado (~500 MB cada) | toda mudança de SDK, patch ou compilador deixava a anterior | removidas as gerações sem ponteiro e as de recibo ausente ou com entradas alteradas; mantidas a atual, as verificadas, as em uso (lease) e as de outro dono |
| Identidade do SDK compartilhado por título | `${sourceDir}/../..` diferente em cada título criava uma geração por título | valores de preset que são caminhos únicos são normalizados; os títulos compartilham uma geração |
| `PublishNative` (exe/DLLs para o recibo) | cópia, pulando arquivos idênticos | mantida: neste teste o build fica em C: e a instalação em D:, e o publicado precisa ficar íntegro enquanto a árvore é recompilada |
| Biblioteca por sessão, payload da instalação, codegen do título | cópia verificada | mantidas: conteúdo gravável, backup para rollback e isolamento da geração do SDK; o codegen e o `PublishNative` pulam arquivos idênticos |

## 4. Medições

### Ambiente

Instalação limpa em D: (NTFS sem nomes 8.3):

`D:\x360n-path-validation\B - Instalação — 測試 — ゲーム 🎮\Pasta com nome muito longo para testar caminhos do Windows - Ação 1\…\Ação 4\x360-native`

São 337 caracteres e 363 bytes. Os arquivos mais profundos chegam a 472
caracteres em `.local\user` e 416 em `.local\inputs`. O ambiente A tinha a mesma
profundidade e a `main` `8d5a8c4`; o ambiente B, este trabalho.

Em A só foi exercitada a importação. Em `8d5a8c4`, o código recusa preparar
acima de 141 bytes, conforme a v1.28.18.

### Importação

Bytes escritos medidos pelo `ActionTrace`:

| Passo | A (antes) | B (agora) |
|---|---|---|
| `import.game` Castle Crashers (STFS), primeira vez | 144.901.327 | 1.083.255 |
| `import.game` reimportação do mesmo pacote | 312.925 | 312.054 |
| `.local/inputs` ao final | 724 arquivos, 172.364.317 bytes | 724 arquivos, 172.364.317 bytes |

O conteúdo importado é idêntico nos dois ambientes. Reimportar não criou
duplicatas.

### Preparação no ambiente B

| Passo | Resultado |
|---|---|
| Castle Crashers, primeira preparação | concluída em 9 min 32 s |
| Dashboard 17559 | preparada e iniciada pelo alias: animação de boot e "sign in or out" com os perfis locais |
| Dashboard, gerações antigas do SDK | `retired-shared-sdk` ×3, deixadas pelas mudanças de patch desta etapa |
| Castle Crashers, nova preparação | `reused-shared-sdk`, concluída em 3 min 29 s; uma única geração restante |
| Reabrir o launcher e preparar o Castle de novo | só verificação (≈12,6 s); exe e `.ninja_log` com horário inalterado |
| `prepare-tools.ps1` | 5 arquivos liberados, 961.225.446 bytes (LLVM 862.053.924; CMake 46.473.549; MinGit 39.806.486; Python 12.601.679; Ninja 289.808) |
| `prepare-tools.ps1` de novo | `tools-ready` sem baixar nem liberar nada |

### Armazenamento

Arquivos lógicos medidos sem seguir junctions. Hardlinks contam uma vez por
pasta. Medições: `storage-after-prepare.tsv` e `storage-after-release.tsv`.

| Pasta | Arquivos | Bytes antes da liberação | Bytes depois |
|---|---|---|---|
| `.local/inputs` | 724 | 172.364.317 | 172.364.317 |
| `.local/build` (publicados) | 25 | 162.437.008 | 162.437.008 |
| `.local/user` | 743 | 150.316.273 | 150.316.273 |
| `.local/cache` | 7 → 2 | 961.225.651 | 205 |
| `titles` (inclui código gerado) | 731 | 244.396.597 | 244.396.597 |
| `x360n\t\<id>` (ferramentas) | 12.910 | 4.369.151.858 | 4.369.151.858 |
| `x360n\e\<id>` (SDK) | 20.849 | 1.731.100.238 | 1.731.100.238 |
| `x360n\s\…` (uma geração do SDK) | 1.553 | 523.624.574 | 523.624.574 |
| `x360n\b\…` dashboard | 670 | 969.130.412 | 969.130.412 |
| `x360n\b\…` Castle Crashers | 248 | 228.062.969 | 228.062.969 |

## 5. Testes

Todos em Debug sobre o build desta árvore. Nenhuma suíte completa foi rodada.

| Teste | Resultado |
|---|---|
| `x360n_tests "[paths],[fs-io]"`: até 2048 caracteres, Unicode, UNC real (`\\localhost\<unidade>$`), junctions, `PlaceDependencyFolders`, profundidades medidas contra este checkout | 21 aprovados, 1 pulado (symlink exige privilégio que a conta não tem), 1179 asserções |
| `x360n_tests "[preparation]"`: inclui retirada de gerações e normalização de presets | 16 casos, 137 asserções |
| `x360n_desktop_tests`: inclui importação por hardlink da extração | 101 casos, 2930 asserções |
| `x360n_sdk_checks` | 35 casos, 1984 asserções |
| ctest focado: MAX_PATH (7), forma host, `Host paths use`, `Launcher and runtime agree`, `short junction`, `ReXApp shuts down` (2), janela persistente, perfis (21), `native.abi` | todos aprovados |
| `[.local-system-update]` com o ZIP 17559 original do usuário | 48 asserções, 3 variantes |

O controle negativo dos testes de SDK (0253–0255) falha sem o patch.

## 6. Limitações comprovadas

- **Rede e sistemas sem junction.** Instalação profunda em compartilhamento de
  rede, ou em sistema de arquivos sem junctions, é recusada com a causa.
  `%LOCALAPPDATA%` precisa ser curto o bastante para as pastas `t`, `e` e `b`.
- **Hardlink exige o mesmo volume.** Entre volumes diferentes, a importação
  continua copiando, uma única vez.
- **Gerações do SDK mantidas.** Uma geração cujas entradas ainda conferem é
  mantida mesmo sem título que a use (por exemplo, após mudar só a identidade do
  compilador). Ela é removida quando suas entradas mudam.
- **Primeiro início depois de uma atualização.** O início automático registra
  uma verificação reprovada antes de preparar. É o comportamento atual,
  preservado.
- **System Update USB pelo launcher.** Termina pedindo os componentes ausentes
  (bases de fonte), igual na `main` anterior.
- **SDK local.** Nesta validação o SDK foi semeado de um checkout local, não
  baixado. Arquivos baixados de ferramentas também vieram de cópia local.
- **Gameplay.** No ambiente profundo o Castle Crashers chegou à Guide e à
  seleção de personagem; a partida em si não foi confirmada.

## 7. Problemas encontrados no uso pelo launcher

- **Host de UI da sessão (`982f30c`).** No ambiente profundo o Castle Crashers
  parava com "Missing verified dashboard system UI host" (0xC0000409): o título
  abre o host com `LoadLibrary`, que não aceita nome além de `MAX_PATH`. O
  supervisor passa agora o nome pelo alias da instalação. Teste
  `titles load the session UI host of a deep installation`: falha sem a
  correção (337 caracteres, carga recusada) e passa com ela.
- **Entrar perfil pela Guide (patch 0256, `e62a801`).** Não depende do caminho
  profundo: os logs do checkout curto de 31/10 a 02/11 mostram a mesma falha.
  O `signin.xex` original pede `XamAllocEx` com a classe 0x24400000, recusada
  pelo adaptador com 80004001 ("signin original initialize -> 80004001",
  "queued request 00021001 -> 80004001"). No `xam_ui.xex` 17559, 816E67E8 só
  separa o bit físico 0x40000000; as demais classes seguem o caminho virtual
  (816E5F10, 816E5E40 pool 6, 816E5D58 zera o bloco). O sdkcheck
  `[signin-allocation]` falha sem o patch e passa com ele. O Sign In na tela
  **não foi exercitado** depois da correção.
- **F3 fechava o título (patch 0257, `4125f87`).** Reproduzido numa sessão da
  dashboard com captura de crash e F3 enviado por script, com autorização do
  mantenedor: `STATUS_HEAP_CORRUPTION` no primeiro F3, detectado num free dentro
  do driver D3D12; com apresentação só pela thread de UI, no 8º F3, dentro do
  DXGI. F4 (configurações) sobreviveu a 30 alternâncias. Causa:
  `DebugOverlayDialog` declarava o histórico de frames só com
  `REXGLUE_ENABLE_PERF_COUNTERS`, definido apenas no diretório do SDK; os
  títulos compilam `rex_app.cpp`, que aloca o overlay, sem a macro, e o
  construtor do runtime escrevia cerca de 488 bytes além da alocação. Com os
  membros declarados em todo build, a mesma sessão sobreviveu a 40 F3, com o
  overlay aberto na captura. O sdkcheck `[debug-overlay]` falha sem o patch
  (3 falhas) e passa com ele.
- CI de `982f30c` e `e62a801` aprovada nas seis plataformas.
