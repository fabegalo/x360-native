# Sistema de avatares: contrato observado (etapa de leitura)

Data: 2026-10-12. Objetivo: entender o que os títulos em teste exigem do XAM para
avatares antes de implementar qualquer coisa. Nada foi implementado; o SDK continua
devolvendo falha em `XamAvatarInitialize`.

Entradas, todas originais e não alteradas:
- XAM 2.0.17559.0 (`dashboard/versions/17559/retail_system_assets/extracted/xam.xex`),
  lido como referência com `x360n-content dump-image/disasm/xref/callers/exports`;
  imagem em 0x815F0000. **Não executado.**
- Pacote de recursos de avatar 17559 (`storage_packs/FFFE07DF00000002`, extraído do zip
  oficial; conteúdo em `storage_packs/FFFE07DF00000002.extracted/`, 0 erros).
- Motocross Madness `default.xex` (`2a8406bc…5dcd`) e A World of Keflings
  `default.xex` (`c49b1bba…7ae0`).
- Referência Xenia Canary `8d8ba9b6f71e`, `xam_avatar.cc`, consultada **só** para
  conferir assinaturas.

Marcação: **F** = fato (desmontagem, dados ou log); **H** = hipótese.

## 1. Funções usadas

Os dois jogos importam as mesmas 10 funções (F: tabela de imports do SDK loader). Cada
uma tem um único wrapper na biblioteca de avatares compilada no jogo (Motocross:
`0x82957BC0..0x82958B3C`).

| Ordinal | Função | XAM 17559 | Como o XAM a implementa (F) |
|---|---|---|---|
| 0x5DC | XamAvatarInitialize | `0x816755E8` | reserva parte do buffer do título para a app interna 0xF2 (`0x816BD7A0(0xF2, buf+0x100000, size-0x100000, 1)`) e envia a mensagem **0x600001** com 40 bytes (`0x816A2CF0`); desfaz a reserva se falhar |
| 0x5DD | XamAvatarShutdown | `0x81674940` | `b 0x816BD6A0(0xF2, 1)`: libera a app |
| 0x5DE | XamAvatarGetManifestLocalUser | `0x81674950` | app **0xF3**, mensagem 0x600003, 8 bytes; overlapped ou espera |
| 0x5E0 | XamAvatarGetAssetsResultSize | `0x81674A70` | app 0xF2, 0x600005, síncrona (`0x816A2F88`) |
| 0x5E1 | XamAvatarGetAssets | `0x81674AD8` | app 0xF2, 0x600006, **1016 bytes**, assíncrona |
| 0x5E2 | XamAvatarGenerateMipMaps | `0x81674CC0` | app 0xF2, 0x600007, 16 bytes, assíncrona |
| 0x5E3 | XamAvatarSetCustomAsset | `0x81675038` | app 0xF2, 0x60000A, 6 argumentos, síncrona |
| 0x5E7 | XamAvatarLoadAnimation | `0x81674D48` | app 0xF2, 0x60000F, 24 bytes, assíncrona |
| 0x5EB | XamAvatarGetMetadataRandom | `0x81675258` | app 0xF2, 0x600010, 12 bytes (inclui a constante `0x20080002`), assíncrona |
| 0x5F1 | XamAvatarManifestGetBodyType | `0x816753C8` | local, sem mensagem: `0x8173EE60` → `0x8196CA80` |

Parâmetros (F pelos chamadores nos jogos e pelo tamanho das mensagens; nomes conferidos
com o Xenia):
- **Initialize**(versão, sistema de coordenadas?, processador, callbacks[5],
  buffer, tamanho). Motocross: `(4, 1, 5, 0x7018F2E0, 0x41290030, 0x300000)` (F: log da
  execução 4). Keflings: versão **2** (F: `li r3,2` em `0x822F9818`). O buffer de 3 MB
  vem do título (`0x82AA67C8` em Motocross); o XAM usa a parte após 1 MB.
- **GetManifestLocalUser**(user_index, metadata[1000], overlapped). App 0xF3: perfil.
- **GetMetadataRandom**(body_type, count, metadata[], overlapped).
- **GetAssetsResultSize**(component_mask, &result_size, &gpu_resource_size).
- **GetAssets**(metadata[1000], component_mask, flags, result_buffer,
  gpu_resource_buffer, overlapped): 1000 + 4 × 4 = 1016 bytes de mensagem (F).
- **GenerateMipMaps**(assets, flags, buffer_size, mip_buffer, overlapped): 4 × 4 = 16.
- **LoadAnimation**(asset_id[16], flags, output, overlapped): 16 + 2 × 4 = 24.
- **ManifestGetBodyType**(metadata) → tipo de corpo.
- **SetCustomAsset**(buffer_size, data, color_count, colors, metadata, …).

Estruturas: `X_AVATAR_METADATA` tem 1000 bytes (F: tamanho na mensagem 0x600006 e nos
arquivos originais `Avatar6400/6404/6411.Avatar` do pacote `FFFE07DF00000006`). Seu layout
interno **não** foi lido.

## 2. Chamadas observadas (instrumentação)

`[[midasm_hook]]` nos 11 pontos de chamada do Motocross
(`titles/motocross_madness/analysis/motocross_madness_avatar_trace.toml`), apenas leitura de
registradores. Execução `20261012T140336Z-mm-run-4-avatar-trace` (45 s, sem entrada):

```
avatar-trace: XamAvatarInitialize(0x4, 0x1, 0x5, callbacks=0x7018F2E0, buffer=0x41290030, size=0x300000)
avatar-trace: XamAvatarInitialize -> 0xFFFFFFFF
avatar-trace: XamAvatarShutdown(...)
```

Antes do bloqueio só rodam Initialize (falha) e Shutdown (limpeza). As outras 8 nunca são
chamadas (F, coerente com 0 linhas STUB delas nas execuções 2–4).

Depois de um Initialize bem-sucedido (F, estático): o gerenciador de avatares do jogo
`0x82436DC0` chama GetManifestLocalUser 1× e GetMetadataRandom 4×, e chama 2× a carga de
assets `0x82433E50`, que usa GetAssetsResultSize, GetAssets, GenerateMipMaps e o tipo de
corpo; LoadAnimation vem de `0x82958DE8`. `0x82436DC0` é chamado por 5 entradas pequenas
(`0x82437658..0x824377B8`). **Em que momento do jogo isso ocorre não foi observado.**

Keflings: mesmas 10 importações, mesmo buffer de 3 MB, versão 2. **Não executado** (o
título ainda não tem projeto AOT).

## 3. Caminho do XAM até o pacote

F, por chamadas diretas: Initialize → mensagem 0x600001 para a app 0xF2 → tratador
`0x81965AC0` → `0x819652F8` → `0x8196B9A8` → `0x8196B8C8` (escolhe a origem) →
`0x8196B728` (monta) → leitores do TOC.
- `0x8196B8C8`: primeiro o pacote instalado (`0x816E8D40`, não aprofundado); se falhar,
  `"%s\AvatarAssetPack"` com a raiz `"\System??\GAME:"` (a cópia que o próprio jogo traz).
- `0x8196B728`: monta o pacote como `\System??\AvatarAssetPack` (`0x816EFD80`), abre
  `AvatarAssetPack.toc` e `AvatarAssetPackLegacyV1.toc` e confere a versão por chamada virtual.
- Leitor: objeto de 432 bytes (`0x8197D2F0`) → `0x8197D060` (abre com acesso 0x80000000,
  OPEN_EXISTING, `0x8197E718`) → `0x8197CA98` (cabeçalho). O TOC principal passa ainda por
  três camadas (`0x8197D400`, `0x81981290`, `0x81980960`) registradas por `0x8197D7D0`.

## 4. Formato do `.toc`

`0x8197CA98` lê 16 bytes, volta ao início e escolhe o leitor pelo GUID (F):

| GUID | Arquivo | Leitor | Cabeçalho |
|---|---|---|---|
| `580a07d6-4bcd-40be-bf37-254be826fb0b` | `AvatarAssetPack.toc` 17559 | `0x8197C6F8` | 48 bytes |
| `8a762df4-c067-4b66-b4ee-56466b1b8280` | `LegacyV1.toc`; `.toc` principal da 13604 | `0x8197C698` | 40 bytes |
| `9ad6ebce-6262-4eeb-8a82-a3f70b817369` | não visto nos arquivos | `0x8197C638` | 32 bytes |

GUID desconhecido → `0x80070057`. Para os cabeçalhos curtos o leitor preenche o campo
+0x28 com 13 e, no formato de 32 bytes, o campo +0x20 com 1 (F).

Formato atual (17559, 14 662 168 bytes), F por aritmética exata sobre o arquivo:

| Região | Faixa | Conteúdo |
|---|---|---|
| cabeçalho | `0x00..0x30` | GUID; 8 bytes `01c8f109a19cb2e0` (iguais no legado); `u32` 2; **`u32` 1282 = número de registros**; `u32` 2; `u32` 0; **`u32` 18 = colunas**; `u32` 0 |
| registros | `0x30..0x53E48` | 1282 × 0x10C (`0x30 + 1282 × 0x10C = 0x53E48`) |
| dados por coluna | `0x53E48..0x9DFEC` | apontados pelos 18 `u32` em +0xBC..+0x103 de cada registro |
| blobs | `0x9DFEC..0xDFBA18` | **+0x104 = offset, +0x108 = tamanho**, contíguos; o último termina exatamente no fim do arquivo |

Registro (0x10C bytes): +0x00 `u32`, valores 1, 2, 8, 0x400, 0x400000 (H: máscara de
categoria/componente); +0x04..+0x07 bytes (1,1,1,0), (2,2,1,0)… (H: compatibilidade de
corpo); +0x08..+0x58 quase sempre zero; +0x98..+0xAB preenchido em ~400–670 registros, com
os 8 bytes `c1 c8 f1 09 a1 9c b2 e0` (o carimbo do cabeçalho com os 2 bits altos ligados,
como o ID de 128 bits que `0x8197E240` monta; H: identificador do asset); +0xBC 18
offsets; +0x104/+0x108 blob.

Blobs: todos começam com `STRB 01 00` + GUID `59bde08b-4909-4ee4-b991-ada87b7c0b6b` +
`01 04`; tamanho de 0 a 111 276 bytes, média 10,9 KB. Os itens de avatar que os jogos
trazem (`asset_v2.bin`, `f_asset_v2.bin`… do Charlie Murder) começam com os mesmos 24
bytes (F): mesmo formato de asset binário. Quem interpreta `STRB` é a biblioteca de
avatares do jogo, não o XAM (H, coerente com o XAM só copiar blobs para o buffer do título).

Não lido: a área de dados por coluna, o significado das 18 colunas, o layout exato do
ID, o formato legado V1 além do cabeçalho.

## 5. Tamanho do código original

- Região do avatar no XAM (`0x81964000..0x81984000`): **473 funções, 118 KB** de código
  PowerPC (676 funções / 180 KB em `0x81960000..0x81990000`). F, pela `.pdata`.
- Alcançável por chamadas diretas a partir do tratador de Initialize: 108 funções, 23 KB,
  das quais só 26 na região do avatar; o resto é infraestrutura do XAM (framework de apps,
  memória, montagem, sincronização). A partir das 10 exportações + tratador + leitores do
  TOC: 172 funções, 34 KB, mais 31 chamadas virtuais não seguidas, que é onde ficam os
  tratadores reais e as camadas do TOC. É um limite inferior.

## 6. Avaliação

**O que precisa existir para o Motocross passar do aviso.** O bloqueio direto é só o
Initialize. Mas um Initialize bem-sucedido leva o jogo ao seu gerenciador de avatares, que
chama em sequência GetManifestLocalUser, GetMetadataRandom (4×), GetAssetsResultSize,
GetAssets, GenerateMipMaps, o tipo de corpo e LoadAnimation. Devolver sucesso no Initialize
sem essas funções reais seria sucesso fictício: os stubs do SDK nem definem `r3`.

| Função | Classificação | Motivo |
|---|---|---|
| Shutdown | genérica, trivial | libera o estado da app |
| Initialize | genérica | formato do TOC agora conhecido; falta só confirmar como o XAM usa o buffer do título (o primeiro 1 MB e o resto) |
| ManifestGetBodyType | genérica, pequena | leitura de um campo do metadata (offset ainda a ler em `0x8196CA80`) |
| GetAssetsResultSize / GetAssets | genéricas, médias | busca por ID no TOC e cópia dos blobs `STRB`; falta o layout do resultado e do buffer de GPU (tratador 0x600005/0x600006) |
| LoadAnimation | genérica? | ID → blob de animação; não se sabe ainda de qual pacote vem |
| GetMetadataRandom | **depende do XAM** | regras de geração aleatória sobre o TOC (tratador 0x600010) não lidas |
| GetManifestLocalUser | **depende de estado do XAM** | app 0xF3, avatar do perfil; perfis locais não existem no projeto |
| GenerateMipMaps | **depende de estado complexo** | gera mipmaps de texturas no buffer de recursos de GPU do título; formato e tiling não lidos |
| SetCustomAsset | adiável | nenhum chamador nas rotas do gerenciador identificadas |

**Tamanho do primeiro incremento funcional** (H, sem medição): não é pequeno. Seria uma
app 0xF2/0xF3 no framework de apps do SDK, com leitor de TOC, busca e cópia de assets,
metadata, geração aleatória, animação e mipmaps. Antes disso é preciso ler 5 tratadores do
XAM ainda não lidos (0x600003, 0x600005, 0x600006, 0x600007, 0x600010). Estimativa de
ordem de grandeza: vários dias de engenharia reversa mais 1–2 mil linhas de C++ com testes,
e o resultado só pode ser validado vendo um avatar renderizado.

**Conclusão desta etapa.** A reimplementação parcial **não é simples**. Initialize, Shutdown,
o tipo de corpo e a busca de assets podem ser genéricos no runtime, mas o caminho que o
jogo segue logo depois depende de três partes com estado interno do XAM: perfil/manifesto
(0xF3), geração aleatória e mipmaps de GPU. Esta etapa para nessa fronteira, conforme
combinado, para comparação com o caminho 2 (AOT do código de avatar do `xam.xex`). Para o
caminho 2, os números relevantes são os da seção 5: 473 funções na região, e uma
dependência direta de infraestrutura do XAM (framework de apps, heaps, namespace
`\System??`, montagem de pacotes) já no Initialize.

## 7. Implementação do caminho 1: estado (2026-10-12)

Código em `titles/common/avatar/` (C++ puro, sem runtime; compilado pelos títulos e pelos
testes). Nenhuma exportação do SDK foi alterada: `XamAvatarInitialize` continua falhando
até que as funções que o jogo chama depois dele sejam reais.

### Feito, com evidência e testes

| Peça | Arquivo | Fonte | Testes |
|---|---|---|---|
| Leitor do catálogo (3 formatos, registros, nomes por idioma, blobs, posições livres) | `avatar_toc.{h,cpp}` | §4; a contiguidade foi corrigida pelo teste local: 179 posições livres com blob 0/0 | 4 sintéticos (`x360n.avatar catalog:*`); `local.17559 AvatarAssetPack.toc…` (1282 registros, 1103 `STRB`, 179 livres) e `…LegacyV1.toc…` (44) |
| ID de asset ↔ registro | `avatar_metadata.{h,cpp}` | ID = {categoria = `field_00`, índice do registro, 0, byte +4, carimbo \| 0xC…}; 21/21 IDs dos descritores originais conferem, na 17559 e na 13604 | sintético; `local.13604 avatar descriptors resolve against the 13604 catalog` (mesma versão) |
| `XamAvatarManifestGetBodyType` | `BodyType()` | XAM `0x8196CA80`: `+0x120` comparado às constantes `0x81632E68`/`0x81632E78` (os IDs MaleBody/FemaleBody) | sintético + local |
| `XamAvatarGetAssetsResultSize` | `ResultSizeFor()` | XAM `0x81965EC0` + tabela de 13 componentes em `0x816332B8` | sintético |

Outros fatos novos:
- **Colunas = idiomas.** As 18 colunas (13 no legado) são o nome do asset por ID de idioma,
  em UTF-16BE terminado em nulo e compartilhado entre colunas (coluna 1 inglês, 2 japonês, 3
  alemão… 17 chinês simplificado).
- **Descritor de 1000 bytes.** `+0x0C..+0xFB` têm 15 posições de ID de componente,
  `+0xFC..+0x11F` 9 cores ARGB e `+0x120` o ID do corpo. `+0x04`/`+0x08` e `+0x8C` (um
  `0x3F800000` em Avatar6400) não foram interpretados.
- **Pacote `FFFE07DF00000006` da 17559** ("DashPack", versão `0x20449700`): extraído do zip
  oficial para `storage_packs/`. Não traz descritores `.Avatar`, só papéis de parede e
  índices.
- **Despacho das mensagens.** As mensagens assíncronas chegam embrulhadas (sistema
  `0x80000007`) ao tratador `0x81965AC0`; a 0x600001 vai para `0x819652F8`, e as outras viram
  tarefas da fila do worker (`0x819659D0` → `0x819658E8`). O descritor de cada uma vem de
  `0x81964F70` (tabela de bytes em `0x81632DC0`, descritores de 8 bytes em `0x81632D78`):

  | Mensagem | Tratador |
  |---|---|
  | 0x600006 GetAssets | `0x81964D30` → `0x81969848(máscara, flags, resultado, gpu, metadata)` |
  | 0x600007 GenerateMipMaps | `0x81964E20` |
  | 0x60000F LoadAnimation | `0x81964E70` |
  | 0x600010 GetMetadataRandom | `0x81964F30` → `0x8196B5C8` → método virtual +52 do catálogo (`0x81634384` slot 13 = `0x8197D710`, mais um nível virtual) |
  | 0x600008, 0x60000B, 0x60000C, 0x60000D, 0x60000E | `0x81964D80`, `0x81964DD0`, `0x81964EA8`, `0x81964EF0`, `0x81964F28` |
  | 0x600005 / 0x60000A (síncronas) | `0x81964D00` / `0x81964CE0`, só com a app inicializada (`0x81965270`; senão `0x8000FFFF`) |

- **Callbacks do título.** As 5 funções passadas ao Initialize (Motocross: `0x8295B630`,
  `0x8295B720`, `0x8295B3C0`, `0x8295B458`, `0x8295B500`) montam texturas e buffers na
  biblioteca D3D do jogo (`0x82B01510`, `0x82B01E88`, `0x82B12798`). O XAM original as chama
  para criar os recursos de GPU dos assets.

### Tamanho do que falta (alcance por chamadas diretas, limite inferior)

| Implementação | Funções | Código | Chamadas virtuais não seguidas |
|---|---|---|---|
| GetAssets `0x81969848` | 302 | 83 KB | 195 |
| GenerateMipMaps `0x81964E20` | 94 | 50 KB | 58 |
| LoadAnimation `0x81964E70` | 41 | 8 KB | 30 |
| GetMetadataRandom | ≥ 3 + camadas virtuais | — | — |

GetAssets e GenerateMipMaps juntas são ~133 KB de PowerPC que decodificam `STRB` em
recursos de GPU e chamam os callbacks do título. Reescrever isso à mão em C++ com fidelidade
é a maior parte do caminho 1.

## 8. Caminho 2 (híbrido): XAM 17559 original em AOT (2026-10-12)

`titles/motocross_madness` declara o `xam.xex` 17559 original (`78b3dbd8…`) como
`[[modules]]` (`x360nsys:\xam.xex`, tabela de despacho no host, patch 0031). O codegen
recompila o módulo inteiro (19 882 funções na DLL `motocross_madness_xam`, fora do Git),
mas **só são executadas as funções chamadas abaixo**; o ponto de entrada do XAM não roda e
nenhuma exportação do SDK muda. `[project].game_root = .local/inputs`, porque o codegen
recusa módulos fora dele (`project_recompiler.cpp:274-278`); o jogo continua rodando com
`--game_data_root` na pasta do título. Sonda: `src/xam_aot_probe.cpp`, desligada por
padrão (`--motocross_xam_aot_probe`), na thread do jogo, no ponto de chamada do
`XamAvatarInitialize`, restaurando todo o `PPCContext` depois.

**Prova de conceito: 21/21 PASS** (`20261012T150752Z-mm-xam-probe-1`; F):
módulo em 0x815F0000+0x610000; registro de 0x816753C8/0x81670000 e ausência de
0x816753CC; `.rdata` 0x81632E68 = ID do MaleBody; jogo→XAM: `XamAvatarManifestGetBodyType`
original sobre descritores do catálogo 17559 = `BodyType()` C++ (0, 1, 2); XAM→jogo: o
iterador 0x81670000 chama o construtor do jogo 0x82450340 (4 elementos com 0x8200131C, demais
bytes intactos); r1, r2, r13–r31 e f14–f31 preservados em cada chamada; endereço não
registrado → 0xDEADBABE sem tocar a memória; os registradores do hook são o `PPCContext` da
thread (verificado por endereço).

**`XamAvatarInitialize` original** (experimento `--motocross_xam_aot_probe_initialize`,
buffer privado de 3 MB; o jogo continua recebendo a falha do SDK). Cada linha é uma
execução; o passo acrescentado é código original do XAM, na ordem do seu `DllMain`
(0x81696AF0) e da sua partida (0x816968A8):

| Acrescentado | Resultado |
|---|---|
| nada | 0x8007000E: a tarefa da mensagem sai do heap de sistema 1 (tabela 0x81AABFF8), não criado |
| heaps de sistema 0x816E6618 | violação de acesso: lê 0x8E03860A (0x8168AF28, `lhz`, bit 2) |
| + página 0x8E030000 zerada (**H**, a mesma hipótese do perfil 17559 da dashboard) | heaps e init do CRT do XAM concluem (360 seções críticas, 124 objetos, 8 heaps); nulo +0x24 no agendador 0x81AA08A8 (6 ponteiros de threads de trabalho) |
| + agendador 0x816BA650 (TLS do app = 1, 3 threads) e heap do título 0x816E6870 | 0x80070057: o envio (0x816A2F6C) não acha a app 0xF2, slot +0x08 = 0 |
| + handle do módulo 0x816E0B58(`hmodule_ptr`) | a mensagem 0x600001 chega à app **na thread do XAM**; heap da app criado no buffer do título; 0x8196D5D0 → 1, 0x81968640(buffer) → ≥0, 0x8196B3D8 → 0; nulo em 0x8169CE70 |
| + gerente de títulos 0x816A17B8 | volta 0; a alocação de 6536 bytes (0x816E6C78) falha; mesmo nulo |

`avatarcore.xex` (visto no rastreio) é o nome da app interna 0xF2 na tabela do XAM
(0x815F9270: nome, app, tratador 0x81965AC0, sinalizador 0); não é arquivo a obter.

**Bloqueio concreto atual.** A escolha do pacote (0x8196B8C8) pergunta ao gerente de
títulos do XAM (`[0x81A9F850]`, estado 5 e ID em +652) se o título em execução é a dashboard.
Esse objeto pertence ao ciclo de vida de títulos do próprio XAM, que aqui é do SDK
(`XamLoaderLaunchTitle` etc.). Depois dele, o mesmo caminho consulta o gerenciador de
conteúdo do XAM (pacote instalado, 0x816E8D40) e o namespace `\System??` (0x816EFD80). Ou
seja, para o Initialize original concluir é preciso que o XAM tenha estado de título e de
conteúdo coerente com o do SDK — dois donos para o mesmo estado. Não foi escrito nenhum
campo à mão.

**Comparação.** Caminho 2 executou de fato o código original (framework de apps, filas,
heaps, threads) com 5 passos de partida e 1 hipótese, e cada novo passo revelou outra
dependência global do XAM. Caminho 1 (C++) já cobre leitor do catálogo, IDs, tipo de corpo
e tamanhos, com testes, sem depender desse estado; o que lhe falta é o núcleo pesado
(GetAssets/GenerateMipMaps, ~133 KB PowerPC). Hipótese para decidir: híbrido **por função**
— o C++ assume o ciclo de vida (Initialize/Shutdown, estado da app) e chama em AOT só as
rotinas puras de dados do XAM (decodificação `STRB`, montagem de recursos, mipmaps) com o
contexto que elas leem, em vez de tentar fazer o XAM inteiro reconhecer o título. Não
verificado: quanto estado global essas rotinas leem.

## 9. Híbrido por função: dependências de GetAssets/GenerateMipMaps e primeiro teste (2026-10-12)

Divisão adotada: o SDK continua dono do estado do console; o C++ fica com ciclo de vida,
catálogo, IDs, tipo de corpo e tamanhos (§7); o AOT do XAM só executa rotinas cujas entradas
o C++ consegue fornecer. Caminho do Motocross (F, desmontagem):

| Parte | Função | Depende de |
|---|---|---|
| orquestração `GetAssets` | `0x81969848` | contexto de 0x2E210 bytes do pool `[0x81B4B1DC]`, ID do console (`0x81A72494`) e cache de perfil (`[0x81A81114]`, virtual +12) para o próprio avatar, cancelamento por virtual — **estado do XAM, fica no C++** |
| carga do blob | `0x8196AFD8` → estados 1–5 (`0x8196AAB0`, `0x8196A2E0`, `0x8196A398`, `0x81969AC8`) | catálogo e pools `[0x81B4B1D4/D8]` do Initialize — **fica no C++** (`AvatarToc`) |
| montador de saída | `0x81966030` | só buffers: rascunhos 0x10000/0x1CA0F, resultado (alinhado a 4), GPU, máscara; tamanhos por `0x81965EC0` (= `ResultSizeFor`) |
| decodificação por asset | `0x81966FC8` → `0x819669A0` / `0x81966C98` (tipo 0x1000) → `0x81966450` | registro do asset (+16 estado, +32..+47 ID, +48 tipo, +400/+404 blob), versão do protocolo `[0x81B4B238]`, tipo de corpo |
| leitura `STRB` | `0x8196D568`, `0x8196DBE8`, `0x8196D5E8`, `0x81930508`, `0x8196E610`, `0x8196EE50` | só memória guest e vtables estáticas — **nenhum global** |
| texturas | `0x81966450` → `0x81974A90` → `0x81978680` | callbacks do jogo `[0x81A975CC..DC]` (gravados por `0x81977C80`): [2] layout da textura, [0] cópia/tiling |
| `GenerateMipMaps` | `0x81964E20` → `0x8196B1E8` → `0x81978C90` | os 5 callbacks, global `[0x81B4B1F8]` (não lido), cancelamento por virtual |

`STRB` é o formato "Structured Binary" do próprio XAM (mensagens em `0x816331EC` e
`0x8163321C`); o jogo tem um leitor equivalente (strings em `0x8209BF14`).

**Primeiro teste real** (`20261012T162158Z-mm-xam-strb-1`, `--motocross_xam_aot_probe_strb`):
o leitor original decodificou **1103/1103** blobs do pacote oficial 17559 (vindos do
`AvatarToc` em C++) — válido, documento aceito, GUID `59bde08b…` e os bytes copiados pelo
leitor iguais aos do blob lidos independentemente; 6618 chamadas sem quebra de ABI;
assinatura alterada e versão 2 rejeitadas pelo XAM com as mensagens originais.

**Rotina de tamanhos do GetAssets** (`20261012T234936Z-mm-avatars-sizes-1`,
`--motocross_xam_aot_probe_sizes`, branch `avatars/xam-aot`): `0x81965EC0(máscara, &resultado,
&gpu)`, chamada pelo montador `0x81966030`, lê só os argumentos e a tabela `0x816332B8`. Em AOT
deu o mesmo que `ResultSizeFor` (C++) nas **8191/8191** máscaras que os wrappers dos jogos
aceitam (13 bits, não nula), sem quebra de ABI. Entrada real dos dois jogos: **0x1FFF**
(Motocross `0x824377D4`, Keflings `0x8223F564`) → resultado 0x3787C, GPU 0xED000, iguais à
conta manual sobre a tabela. Decisão: a versão C++ continua sendo a implementação; chamar a
original não acrescenta nada aqui.

**Próximo passo concreto (não iniciado):** chamar `0x81966030` + `0x81966FC8` a partir do C++ para um asset
real, com o registro montado a partir do catálogo, buffers de resultado/GPU do tamanho de
`ResultSizeFor`, versão 4 e os callbacks reais do jogo. Falta confirmar a semântica de +16 e
+48 do registro (H: +48 = categoria `field_00`) e se outras funções da cadeia de texturas
leem mais campos.
