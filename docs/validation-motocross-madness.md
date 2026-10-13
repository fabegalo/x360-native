# Validação experimental: Motocross Madness (XBLA)

Terceiro alvo de jogo, a partir de arquivos fornecidos pelo usuário em
`.local/inputs/titles/motocross_madness/` (não versionados; organização em
`metadata/LEIA-ME.txt` daquela pasta). Data: 2026-10-12. SDK: ReXGlue v0.10.0
`f5337cdc` com os patches locais 0001–0033 presentes na árvore no momento do build
(vários escritos por outra sessão durante este trabalho); LLVM 22.1.8; build
`RelWithDebInfo`.

## Entrada

| Item | Valor |
|---|---|
| Pacote | `58410B0F/000D0000/7E38A2BCC89C941C7C6BA32DD79E873F42333EF658`, 1 587 388 416 bytes, SHA-256 `09a3173cfeb4cbd44bf58fc0800cae98aa9aa17584cd036d90d203f3b1d76408`; MD5/SHA-1 iguais aos citados (Redump) na nota do `.7z` recebido |
| Cabeçalho STFS (`x360n-content info`) | magic `LIVE`; content type `0x000D0000`; title id `58410B0F`; media id `62541B78`; versão `0x3`; "Motocross Madness" |
| Extração (`x360n-content extract`) | 55 arquivos, 1 577 889 474 bytes, 0 erros |
| `default.xex` | 19 513 344 bytes, SHA-256 `2a8406bcf9c223f9d7da732be617cc7cd46fbb587ef552f7fcacac2049b95dcd`; único executável |
| DLC | "Trick Jam" (`00000002`) extraída à parte em `dlc/trick_jam/`; **não usada** nas execuções abaixo |

## Etapas

| # | Etapa | Resultado | Evidência / classificação |
|---|---|---|---|
| 1 | Identificação | ok | XEX2 `title`; PE `default.exe`; entry `0x82AA5EA0`; base `0x82000000`; imports `xam.xex` 240, `xboxkrnl.exe` 357; XDK 2.0.21119.0 |
| 2 | Leitura pelo carregador do SDK | ok | imagem de 0x1330000 bytes, SHA-256 `c5bdad1b…ca5e` (dois dumps idênticos); `.text` 0x82210000 + 0xF56A8C; `.pdata` com 37 353 funções |
| 3 | Análise sem ajustes | **falha** | `Validation failed: 10 unresolved calls`; todos os alvos são código sem `.pdata` |
| 3' | Diagnóstico | causa identificada | desmontagem: 3 métodos virtuais folha cujo alvo era o próprio epílogo (`li r3,0; blr`), referenciados só por `.rdata` (vtables sem RTTI); 5 thunks de chamada virtual de 4 instruções; 2 funções alcançadas por adjustor thunks. Entradas em `titles/motocross_madness/analysis/motocross_madness.toml` (início real de cada função) |
| 4 | Compilação (1ª) | **falha** | `motocross_madness_recomp.9.cpp: use of undeclared label 'loc_82C687F8'` (+5); `check-pdata-overlap.ps1`: 1 de 37 353 (`sub_82C680E0`) |
| 4' | Diagnóstico | causa identificada | tabela de saltos por byte sem verificação de limites (`lbzx` em `0x821156B0`, `bctr` em `0x82C68124`), **byte a byte igual** à do Castle Crashers; `[[switch_tables]]` manual de 16 entradas. Depois: 0 sobreposições |
| 5 | Ligação | **falha** | 7 × `undefined symbol __imp__XUsbcam*`: `xboxkrnl_usbcam.cpp` não é compilado no SDK fixado (`src/kernel/CMakeLists.txt:53`) |
| 5' | Correção | ok | `XUsbcamGetState` (0 = desconectada) e `XUsbcamCreate` (`X_STATUS_NOT_IMPLEMENTED`) de `titles/common/camera_state.cpp`, com os chamadores deste XEX conferidos (`src/unresolved_imports.cpp`); os outros 5 são armadilhas `REX_FATAL`. Executável ligado |
| 6 | Execução 1 | **falha** | após ~4 s: `[FATAL] Call to invalid or unregistered function at guest address 0x82ACC350` (método virtual sem `.pdata`, só em `.rdata 0x820E0D24`) |
| 6' | Correção | ok | `scripts/check-pointer-targets.ps1`: 189 de 2141 endereços passam em todos os critérios, incluindo `0x82ACC350`; 4 conferidos por amostragem; arquivo gerado separado `analysis/motocross_madness_pointer_targets.toml` |
| 7 | Execução 2 | saída voluntária | 18,5 s, exit 0: o próprio jogo chamou `XLaunchNewImage("XLNI_DASH_ARCADE", 0)` (→ `XamLoaderLaunchTitle(NULL, 0x100)`), pela rotina de encerramento `0x831081F8`. O controle do usuário aparece conectado aos 9 s; nenhuma entrada foi enviada por nós. As capturas desta execução foram tiradas cedo demais e não valem |
| 8 | Execução 3 | **tela de título renderizada; bloqueada** | 60 s até o timeout; 0 críticas; exe SHA-256 `5eac2f7a…12fdd`. Captura `.local/runs/mm-captures/run3c-00s.png`: tela de título original ("PRESS A TO START THE GAME", "featuring avatar famestar") com o diálogo do jogo **"MISSING AVATAR PACK — The game can not continue without an installed avatar pack. (A) EXIT GAME"** |

Comando da execução 3:

```powershell
x360n run .local\build\titles\motocross_madness\motocross_madness.exe --label mm-run-3 --timeout 60 --rex-log -- `
  --gpu_plugin=xenos --game_data_root=<raiz>\.local\inputs\titles\motocross_madness\extracted `
  --user_data_root=<raiz>\.local\user\motocross_madness
```

## Bloqueio atual: avatares

`XamAvatarInitialize` do SDK devolve sempre falha (`src/kernel/xam/xam_avatar.cpp:23-33`,
`return ~0u;`, comentário "Negative to fail. Game should immediately call
XamAvatarShutdown"), e os demais `XamAvatar*` importados pelo jogo são `REX_EXPORT_STUB`.
**Inferência, não verificada no código do jogo:** essa falha é o que leva ao diálogo
"Missing Avatar Pack". Seguir exige implementar o subsistema de avatares do XAM com o
pacote de recursos de avatar original do sistema; isso não foi feito.

A etapa de leitura completa (contrato das 10 funções, formato do `.toc`, instrumentação
e avaliação) está em `docs/avatar-system.md`.

### O que o jogo pede (desmontagem de `default.xex`)

`0x82957C40` é a inicialização da biblioteca de avatares compilada no jogo. Ela valida
os argumentos, reserva 0x300000 bytes (`0x82AA67C8`, guardado em `0x8323A0F8`), preenche
uma tabela de 5 callbacks (`0x8295BB28`) e chama
`XamAvatarInitialize(4, r29, r28, &callbacks, buffer, 0x300000)` em `0x82957D34`; um
resultado negativo desfaz tudo (`0x82957BC0`) e volta como erro. Imports de avatar do
jogo: Initialize, Shutdown, GetManifestLocalUser, GetMetadataRandom,
ManifestGetBodyType, SetCustomAsset, GetAssetsResultSize, GetAssets, GenerateMipMaps,
LoadAnimation.

### Como o XAM original faz (XAM 2.0.17559.0, lido como referência, não executado)

Imagem em 0x815F0000 (`x360n-content dump-image`). Tabela em `.data 0x81A97474..80`:
`"\System??\Asset%p"`, `"\System??\AvatarAssetPack"`, `"AvatarAssetPack.toc"`,
`"AvatarAssetPackLegacyV1.toc"`.
- `0x8196B728(caminho)` monta o pacote como `\System??\AvatarAssetPack` (`0x816EFD80`),
  abre `AvatarAssetPack.toc` (`0x8197E238`) e depois `AvatarAssetPackLegacyV1.toc`
  (`0x8197D400`), conferindo a versão de cada um por chamada virtual.
- `0x8196B8C8` escolhe o caminho: primeiro o pacote instalado (`0x816E8D40`, não
  aprofundado); se falhar, `"%s\AvatarAssetPack"` com a raiz `"\System??\GAME:"`, ou
  seja, a cópia que o próprio jogo traz (A World of Keflings traz uma).
- O formato `.toc` não é documentado; o `AvatarAssetPack.toc` da 13604 tem 13 648 884
  bytes (é a base de recursos inteira, não só um índice).

### Componentes originais disponíveis

Pacote de recursos de avatar: PIRS, content type `0x00008000` (`kSUStoragePack`),
title `FFFE07DF`, arquivo `FFFE07DF00000002` de cada atualização de sistema; a versão do
pacote é a do sistema.

| Origem | Versão | SHA-256 |
|---|---|---|
| `SystemUpdate_17559_USB.zip` oficial, extraído em `dashboard/versions/17559/retail_system_assets/storage_packs/` | `0x20449700` (17559) | `9dcef5b4…cd8d6` |
| disco do Forza Horizon 2, `$SystemUpdate` | `0x20409900` (16537) | `1aebbdbd…bd33b` |
| coleção 15574 (= disco do Forza Horizon) | `0x203CD600` | `4c51e93a…` |
| coleção 13604 / 9199 | `0x20352400` / `0x2023EF00` | `950744bc…` / `3a703102…` |
| A World of Keflings, `AvatarAssetPack` | `0x201CB300` (7347) | `beaaae8d…` |

O perfil 17559 deve usar o pacote do zip 17559, sem misturar versões.

### Referência Xenia

Xenia Canary `8d8ba9b6f71e` (2026-09-12), `src/xenia/kernel/xam/xam_avatar.cc`:
`XamAvatarInitialize` devolve sucesso só com a cvar `allow_avatar_initialization`
("Only set true when testing Avatar games. Certain games may crash"); as demais são
`kStub`, algumas com dados fixos. Não há implementação de referência do subsistema.

## Observações

- **`FileAccess.package` é XCTD comprimido** (começa com `0x0FF512ED`).
  `NtQueryInformationFile(XFileXctdCompressionInformation)` (classe 27) não é
  implementado pelo SDK (`X_STATUS_INVALID_PARAMETER`, `xboxkrnl_io_info.cpp:151-160`).
  O jogo tem um caminho alternativo para isso: em `0x82AB2204` ele lê os 4 primeiros
  bytes e compara com `0x0FF512ED`. A tela de título carrega dados desse pacote; se o
  caminho comprimido funciona por inteiro não foi verificado.
- Arquivos que o jogo procura e não existem no pacote:
  `game:\media\GameplayTexts\ENGLISH\ScaleformTranslation.txt` e `dlc_0.txt`
  (`0xC000000F`). Não foram fatais. O `dlc_0.txt` pode ter relação com a DLC, que não
  estava montada; não verificado.
- `x360n-content xref` caía nesta imagem: `.reloc` termina em 0x83341560, depois da imagem
  carregada (0x83330000). Corrigido limitando a varredura à imagem carregada.
- Builds simultâneos de dois títulos colidem: todos gravam a DLL do SDK em
  `external/rexglue-sdk/out/win-amd64` (`CMakeLists.txt:226-228` do SDK). Duas tentativas
  falharam por isso (header do SDK alterado no meio do build; `rexruntimerd.dll` bloqueada).
- XAM 17559 original em AOT no mesmo processo (caminho 2): prova de conceito 21/21 e
  bloqueio atual do `XamAvatarInitialize` original em `docs/avatar-system.md` §8. Sem as
  opções da sonda, o jogo roda como na execução 3.

### Incremento isolado: transformação STRB de cores (2026-10-12)

`20261013T011122Z-mm-avatars-colors-3`, na worktree `x360-avatars`: o XAM
17559 AOT `0x81976598` expandiu 39 tabelas originais em 63 triplas de cores,
com comparação dos 220 bytes de cada saída contra extração independente dos
bits. Três campos truncados foram rejeitados sem escrita; 291 chamadas
preservaram a ABI verificada. Detalhes, dependências e limites em
`docs/avatar-system.md` §10. O contexto do jogo foi restaurado e
`XamAvatarInitialize` continuou retornando `0xFFFFFFFF`; ainda não há avatar
renderizado nem novo marco de jogabilidade. O timeout após a sonda é controlado.
### Incremento isolado: malhas/texturas e callbacks reais (2026-10-12)

`20261013T012506Z-mm-avatars-mesh-2`: o leitor XAM `0x81974A90` montou os
589 assets de campo 3 (634 malhas, 1619 texturas), usando callbacks reais do jogo:
275 chamadas de tiling e 1612 de layout. Validados 1.189.998 índices e 2.871.296
bytes tiled, buffers/ponteiros/ABI e três negativos. Transporte LZX em C++ com a
biblioteca existente do SDK; imports LDI continuam pendentes. Sonda com zero
falhas; jogo encerrado pelo supervisor em 45 s, com os erros XCTD/devkit já
conhecidos. Sem novo marco de renderização ou jogabilidade. A nova opção
`motocross_xam_aot_probe_meshes` pula as suítes anteriores, que não foram repetidas.
Detalhes e limites em `docs/avatar-system.md` seção 11. Somente `x360-avatars`.

### Incremento isolado: LDI e worker GetAssets (2026-10-12)

`20261013T014125Z-mm-avatars-ldi-1`: patch 0035 implementa o perfil LDI com
workspace do chamador e reset por bloco. Stream comprimido original validado em
avanço/cruzamento de 32 KiB/reset/releitura/destruição. Worker `0x81966450` retornou
HRESULT 0 para MaleBody, FemaleBody e Power Tee, com buffers/índices/callbacks/ABI
válidos. Sonda com zero falhas; 46 verificações sintéticas LDI novas passaram.
Initialize permanece em `0xFFFFFFFF`; GetAssets agregado/export e renderização
pendentes. Nenhuma suíte anterior repetida; detalhes e limites na seção 12 de
`docs/avatar-system.md`. Toda a execução ficou na worktree de avatares.

### Incremento isolado: resultado agregado e ciclo de vida (2026-10-12)

`20261013T015915Z-mm-avatars-aggregate-1`: `0x81967628` inicializou o agregado;
`0x81966FC8/0x819669A0` montaram Power Tee + MaleBody pelo worker e stream LDI
originais; `0x819660A0/0x81966348` finalizaram dois descritores e 71 ossos.
Ponteiros/IDs/limites/propriedade/ABI aprovados. Cancelamento e erro AOT real no
segundo asset descartaram a saída privada; liberação/polling terminal conferidos.
Novo coordenador compartilhado C++ com 61 verificações sintéticas aprovadas.
Nenhuma suíte anterior repetida. Build final passou. Export GetAssets/overlapped,
protocolo 2, componentes especiais/modificadores e renderização continuam
pendentes; Initialize mantém `0xFFFFFFFF`. Perfil diagnóstico 4, forma neutra,
apenas na x360-avatars. Evidência, comando e limites em `docs/avatar-system.md` §13.
