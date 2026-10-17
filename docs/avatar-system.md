# Sistema de avatares: contrato observado

Estado atual: [serviço e gameplay do tutorial](#14-servico-real-e-entrada-no-tutorial-2026-10-13).
As seções iniciais abaixo preservam o registro histórico da investigação.

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

## 10. Transformação real: tabelas de cores STRB em AOT (2026-10-12)

Na worktree `x360-avatars`, branch `avatars/xam-aot`, a opção diagnóstica
`--motocross_xam_aot_probe_colors=true` executa **`0x81976598`** do XAM 17559.
Ela pertence ao caminho `GetAssets`: `0x81966FC8` → `0x819669A0`/`0x81966C98`
→ `0x81966450` → leitura do campo 7 → `0x81976598`. Não é cálculo de tamanho:
expande deltas compactados em triplas de cores ARGB de 32 bits.

- Entrada: `r3` aponta para saída de 220 bytes; `r4`, para o stream do campo 7
  construído por `0x81976080(reader, iterator, 0)` (com o objeto como primeiro argumento).
  O leitor/iterador original é criado sobre o blob inteiro, não sobre dados sintéticos.
- Saída: até 18 triplas de `u32` big-endian, contador em `+216`; retorno 1/0.
- Dependências imediatas: `0x819761B0` (cabeçalho compactado), `0x81976308`
  (expansão), `0x81976298`/`0x81971600`/`0x8197A388` (componentes e bits),
  streams e vtables estáticas do leitor STRB. Sem inicializar heaps, apps,
  gerenciadores de títulos/conteúdo ou o ponto de entrada do XAM.
- Essa operação não precisa de callbacks de GPU. Os 20 bytes da tabela dos cinco
  callbacks em `0x81A975CC` são comparados antes/depois; permanecem intactos.

**PASS**, execução final `20261013T011122Z-mm-avatars-colors-3`:
39/39 tabelas, 63 triplas (189 cores), 291 chamadas sem quebra da ABI verificada
(r1, r2, r13–r31, f14–f31). Três tabelas têm deltas de 24 bits por componente:
registros 72 e 300 (`Power Tee`) e 275 (`Tight Tee`), nove triplas cada. As outras
36 têm uma tripla com larguras zero. No registro 72, campo em `+0x5430`, 109 bytes,
a primeira tripla é `FF3286DA/FF3286DA/FF3286DA`; no 112, as cores são
`FFFF0000/FF00FF00/FF0000FF`.

A referência é uma leitura independente dos bytes originais: cabeçalho de sete
palavras little-endian (contagem, base/largura para cada componente) e deltas
extraídos **bit a bit**, LSB primeiro, somados às bases. Não usa a saída AOT nem
chama o extrator de bits do XAM. Os 220 bytes são comparados integralmente,
incluindo posições não utilizadas; sentinelas de 32 bytes verificam os limites,
e o blob de entrada é comparado integralmente. O endereço da saída e os limites
do iterador são conferidos/registrados em cada caso. O teste não é uma comparação
visual contra console e não demonstra renderização.

Três negativos: em cópias privadas dos registros 72, 275 e 300, reduzir o tamanho
declarado do campo em um byte faz a rotina retornar **0**, sem tocar saída ou
sentinelas. Antes das chamadas válidas, a sonda limita contagem, larguras,
tamanho do payload e endereços. Isso não transforma o leitor original num parser
genérico de entradas hostis.

Fonte: `AvatarAssetPack.toc` do storage pack 17559 já autorizado, SHA-256
`a8c90b3db5c6349a55fcb72eda47e08e4940f88fabda4ff73025d9728832daa8`.
Relatórios em `.local/evidence/runs/` **da worktree de avatares**. O build privado voltou
para `.local/build/titles/motocross_madness-avatars` nessa worktree; o SDK, as DLLs,
o código gerado e o perfil de teste são separados da distribuição. A cópia antiga
do build em `x360` foi preservada. Integração na `main` adiada por escolha do usuário.

Reprodução: usar `x360n --root <worktree> run <exe-privado> --timeout 60 --rex-log --`
com `--motocross_xam_aot_probe=true`, `--motocross_xam_aot_probe_colors=true`,
`--gpu_plugin=xenos`, e os caminhos locais de `--game_data_root`,
`--user_data_root`, `--x360n_xam17559_root` e `--x360n_avatar_catalog_17559`.
O relatório JSON guarda os argumentos completos e hashes do executável/DLLs.
A execução 1 expirou antes da sonda; a 2 validou as saídas; a 3 acrescentou os
negativos. As duas últimas concluíram a sonda com zero falhas, antes do timeout
controlado do jogo. Erros já conhecidos do título (XCTD, devkit) e o endereço
inválido intencional da prova de integração continuam presentes no log.

Regressão: os dez testes existentes `[avatar]` passaram (75 asserções unitárias
e 1189 locais). A prova das 8191 máscaras **não foi repetida**; `ResultSizeFor`
continua em C++. `XamAvatarInitialize` permaneceu em `0xFFFFFFFF`.

**Progresso e próxima fronteira:** a expansão de dados reais de cores está
executada e validada sem gerenciadores globais. Não houve bloqueio nessa rotina.
Falta executar a montagem das malhas/texturas do campo 3 (`0x81974A90`, chamada
por `0x81966450`), validar os ponteiros relocados nos buffers resultado/GPU e os
callbacks reais do título. Essa cadeia completa não foi executada aqui; suporte
funcional aos protocolos 2/4 e um avatar renderizado continuam pendentes.

## 11. Malhas e texturas reais do campo 3, callbacks do jogo (2026-10-12)

Continuação na **`x360-avatars` / `avatars/xam-aot`**, sem integrar a `main`.
`--motocross_xam_aot_probe_meshes=true`, junto da opção principal do probe,
seleciona uma rota exclusiva após montar/carregar XAM e ler o catálogo. Pula as
provas anteriores de integração, tamanhos, STRB e cores. Não combinar com a opção
experimental `motocross_xam_aot_probe_initialize`.

**PASS**, run `20261013T012506Z-mm-avatars-mesh-2`: todos os **589 assets com
campo 3** encontrados nos 1282 registros do catálogo 17559 foram montados pelo
leitor original **`0x81974A90`**. Resultado: **634 malhas, 1619 texturas**,
**1.189.998 índices** dentro do intervalo de vértices da respectiva malha e
**2.871.296 bytes tiled** comparados com uma referência independente. Zero falhas
da sonda e da ABI verificada (r1/r2/r13–r31, f14–f31). Cada stream foi consumido
integralmente, sem leitura fora do intervalo.

### Transporte LZX e fronteira preservada

O stream comprimido original `0x819766B0` depende dos imports de kernel
`LDICreateDecompression`, `LDIDecompress`, `LDIResetDecompression` e
`LDIDestroyDecompression`, ainda stubs no SDK fixado. **Não foram implementados
nem substituídos por sucesso.** A sonda usa `lzx_decompress` já existente no SDK,
com janela de 0x8000 e novo decoder por bloco, como o reset observado em
`0x81976888` após cada descompressão. Cada bloco tem três `u32` little-endian:
tamanho comprimido (até 0x9800), offset descomprimido e tamanho de saída (até
0x8000), seguidos do payload. Limites e contiguidade são verificados antes de
descomprimir; saída total limitada a 16 MiB.

Um adaptador sequencial C++ com cinco slots (destruir, resetar, posição, avançar,
ler) entrega os bytes descomprimidos ao XAM. A leitura é limitada ao stream e
preenche a quantidade realmente copiada. A interpretação/expansão de malhas,
vértices, índices, texturas e a montagem dos descritores continuam no AOT original:
`0x81974A90` → `0x819747D8` / `0x81978680` / `0x81971CA8`. Não é ainda a cadeia
completa do export `GetAssets`, nem valida equivalência do decoder LZX com console.

### Descritores e buffers

O cabeçalho descomprimido contém 12 palavras LE. As primeiras sete viram os
campos `+0..+24` do descritor de saída de 52 bytes. `+28/+32` são os buffers CPU/GPU
privados. As palavras 7/8 são offsets GPU, relocados em `+36/+40`; palavras 9/10
são offsets CPU, relocados em `+44/+48`; palavra 11 informa o scratch necessário.

Cada malha ocupa 512 bytes, com quantidade de triângulos em `+484`, vértices em
`+488`, stride em `+496`, ponteiros de vértices/índices em `+504/+508`. Verificam-se
os intervalos completos, os índices BE de 16 bits e os ponteiros relocados. Cada
textura ocupa 44 bytes: dimensões `+4/+8`, tamanho `+12`, ponteiro `+36`, todos
conferidos contra o buffer GPU. Sentinelas de 32 bytes após cada capacidade
declarada e páginas de guarda por padrão de bytes nas bordas das alocações
permaneceram intactas (não são páginas protegidas pelo SO). Limite privado: 2 MiB
por buffer. Não é validação byte a byte dos vértices contra uma segunda implementação.

### Callbacks reais, sem respostas simuladas

A tabela recebida em `r6` no callsite de `XamAvatarInitialize` é validada contra
os cinco endereços AOT do jogo e instalada pela rotina original **`0x81977C80`**.
Observadores temporários no dispatcher encaminham o mesmo contexto ao código
original e verificam a ABI. Chamadas observadas:

| Slot | Rotina do jogo | Função | Chamadas pelo XAM |
|---|---|---|---:|
| 0 | `0x8295B630` | tiling, via `XGTileTextureLevel` | 275 |
| 1 | `0x8295B720` | detiling | 0 |
| 2 | `0x8295B3C0` | layout de textura | 1612 |
| 3 | `0x8295B458` | tamanho/alinhamento | 0 |
| 4 | `0x8295B500` | cabeçalho/mip | 0 |

A referência calcula o tamanho da linha e o número de linhas pelos blocos do
formato Xenos. Para tiling, confere cada bloco na posição calculada pela fórmula
de endereçamento 2D também documentada em `texture_util::GetTiledOffset2D` do SDK,
contra os bytes lineares no scratch. Não chama o detiler do jogo como oráculo.
Os sete descritores sem callback de layout seguem o ramo de textura zerada.
Os outros três callbacks estão registrados, **mas não exercitados** neste caminho.
A tabela global XAM é restaurada byte a byte, os dispatches voltam às funções
originais, os buffers são liberados e o contexto PPC do jogo é restaurado.

Três negativos novos, no registro 72: capacidade CPU um byte menor retorna 0 e
restaura o descritor; cabeçalho descomprimido de 47 bytes retorna 0 sem alterá-lo;
frame LZX truncado em um byte é rejeitado antes do AOT. A primeira execução
`20261013T012130Z-mm-avatars-mesh-1` apenas estabeleceu a chamada de três assets;
a segunda acrescentou as verificações de conteúdo, limites e negativos e cobriu
todo o campo 3. As suítes já concluídas antes deste incremento não foram repetidas.

Reprodução: mesma linha da seção 10, trocando a opção de cores pela opção de
malhas acima. Build e perfil ficam em `.local/` da worktree de avatares. O
supervisor terminou o jogo por timeout de 45 s após a sonda completar; os quatro
erros XCTD/devkit e quatro stubs de voz conhecidos do título persistem. Isso não
é um resultado funcional do jogo completo. Catálogo original e hash permanecem
os da seção 10; nenhum asset proprietário ou fonte AOT gerada entra no commit.

**Próxima fronteira:** conectar esse caminho ao processamento completo de assets
e à entrega dos buffers ao jogo, decidir a implementação fiel dos contratos LDI
para usar o stream comprimido original, e resolver a inicialização/gerência exigida
por `GetAssets`. `XamAvatarInitialize` continua retornando `0xFFFFFFFF`; protocolos
2/4 completos, avatar renderizado, edição e equivalência visual continuam pendentes.

## 12. Imports LDI e worker original de GetAssets (2026-10-12)

Implementado na worktree `x360-avatars`, sem integração na `main`: patch local
**0035-ldi-caller-workspace**, aplicado por `scripts/setup/apply-sdk-patches.ps1`, com
hashes anteriores/finais em `deps.lock.json`. Substitui os quatro stubs LDI no
runtime privado; nenhum outro gerenciador do XAM é inicializado.

### Contrato implementado e limites

`LDICreateDecompression` aceita o perfil observado em `0x819766B0`: ponteiro para
tamanho de bloco 0x8000, configuração de duas palavras 0x8000/0x8000, callbacks de
alocação/liberação nulos, workspace do chamador alinhado a 16 bytes, tamanho do
workspace e ponteiro de saída para contexto. Exige 0x9800 bytes; se insuficiente,
informa esse tamanho e mantém o contexto de saída nulo. O contexto ocupa somente
16 bytes do workspace e não guarda ponteiro host nem alocação persistente.

`LDIDecompress` aceita blocos independentes com entrada até 0x9800 e saída até
0x8000, via `lzx_decompress` existente no SDK. Usa saída temporária e só publica
os bytes após sucesso. Um erro do decoder devolve contagem zero e deixa o buffer
de destino intacto. `LDIResetDecompression` prepara o próximo bloco, inclusive
após erro; `LDIDestroyDecompression` invalida o contexto sem liberar memória que
pertence ao chamador. Contexto nulo/destruído e tamanhos inválidos são rejeitados.

**Implementação parcial explícita:** outros tamanhos/configurações, callbacks de
alocação e continuação de dicionário sem reset são rejeitados com status não zero.
Não é suporte LDI geral. Sucesso é zero; equivalência dos códigos específicos de
erro do kernel original não foi demonstrada. A necessidade de reset é satisfeita
pelo stream do XAM, que o chama após cada bloco. O nome/interpretação completa dos
campos de configuração fora do perfil observado permanece não verificado.

### Teste sintético novo

`tests/sdkcheck/ldi_test.cpp` chama os quatro imports pela ABI PPC, com um bloco
LZX literal construído pelo próprio teste (sem assets proprietários). Falhou
antes do patch na criação, após duas verificações; depois passou em **46
verificações**: bytes/contagem de saída, sentinelas, reset e nova leitura, truncamento,
falha sem escrita parcial, tamanhos, alinhamento, modo de callbacks não suportado,
dois contextos independentes, destruição, uso após destruição e contexto nulo.

Alvo `x360n_ldi_checks`, teste CTest `sdkcheck.LDI`; disponível no projeto raiz e,
com `-DX360N_LDI_CHECKS=ON`, no build privado do Motocross. Resultado local:
`.local/ldi-synthetic-result.log`. Não foram reexecutadas as suítes anteriores.

### Cadeia real executada

Nova opção exclusiva `--motocross_xam_aot_probe_ldi=true` (com a opção principal
do probe), execução **`20261013T014125Z-mm-avatars-ldi-1`**, zero falhas:

1. Leitor STRB original → campo 3 (`0x81976080`) → stream comprimido original
   **`0x819766B0`**, agora chamando os imports implementados. Verificados leitura
   do cabeçalho, avanço, leitura de 96 bytes atravessando 0x8000, reset, releitura
   do início e destruição (`0x81976648`). A comparação usa a rota de transporte
   anterior; compartilha o decoder LZX, portanto verifica a semântica do stream,
   não constitui uma segunda implementação independente do algoritmo LZX.
2. Worker original **`0x81966450`**, do caminho `GetAssets`, cria os próprios
   streams, chama `0x81974A90`, aplica escolhas de materiais e trata o campo de
   paleta. **HRESULT 0** nos registros 0 (`MaleBody`), 1 (`FemaleBody`) e 72
   (`Power Tee`). Os índices conferidos foram 6792, 7362 e 1860; no registro 72,
   24576 bytes tiled foram comparados com a referência de endereçamento Xenos.
3. Validados cabeçalhos, ponteiros CPU/GPU, limites dos descritores, buffers,
   entrada preservada, contexto LDI invalidado pelo destrutor e ABI verificada.
   Os callbacks reais do jogo permanecem no caminho; tabela e dispatches são
   restaurados ao fim. Não há adaptador C++ substituindo o stream de entrada
   usado pelo worker nessa rota.

O contexto privado de alocação tem dez palavras: capacidades CPU/GPU/scratch/LDI,
respectivos quatro ponteiros e dois cursores CPU/GPU. `0x81966298/0x819662F0`
fazem as reservas originais. A seleção usa a máscara do registro em `+48` e três
escolhas diagnósticas de material (IDs 22–24, RGB 0.5 e alfa 1), explicitamente
inicializadas; **não representa o avatar de um usuário**. Buffers privados evitam
alterar o estado de jogo. A suíte ampla anterior de 589 assets não foi repetida.

Build incremental passou. O supervisor encerrou o jogo após 45 s, depois da sonda
concluir, com os quatro erros XCTD/devkit e quatro stubs de voz conhecidos; nenhum
stub LDI foi chamado. `XamAvatarInitialize` continuou em `0xFFFFFFFF`.

**Próxima fronteira concreta:** `XamAvatarGetAssets` ainda é stub em
`xam_avatar.cpp`; o export original envia a mensagem 0x600006 à app 0xF2. Falta
ligar o worker validado à orquestração C++ e ao resultado agregado por protocolo,
preservando conclusão/cancelamento e propriedade dos buffers. As camadas
`0x81966FC8`/`0x819669A0` e o formato agregado dos protocolos 2/4 ainda não foram
exercitados nesta etapa. Sem sucesso artificial no Initialize, avatar renderizado
ou alegação de `GetAssets` completo.

## 13. Resultado agregado e ciclo de vida GetAssets (2026-10-12)

Incremento apenas na `x360-avatars`, branch `avatars/xam-aot`. Novos componentes
compartilhados em `titles/common/avatar/avatar_assets_request.*` e
`avatar_aot_assets.*`; nenhuma alteração de SDK ou de `Initialize` nesta etapa.

O coordenador C++ `AssetsRequest` executa preparação → decodificação de todos os
assets → aplicação dos campos dependentes → finalização. Uma chamada `Step`
executa uma fase/asset. O status fica pendente (`0x8000000A`) até a finalização;
endereços do resultado só ficam visíveis após sucesso. Falhas mantêm o HRESULT
recebido e liberam toda a montagem privada. Cancelamento cooperativo entre passos
retorna `0x80004004`, sem publicar saída parcial. Polling de estado terminal não
reexecuta o AOT. `Release` é idempotente; a destruição libera inclusive requisições
pendentes. Endereços publicados são emprestados até `Release`/destruição, e todos
os buffers (CPU, GPU, scratch, LDI, controle e cópias de entrada) pertencem à
requisição. O catálogo e o runtime precisam sobreviver a ela.

### Backend original e resultado

O adaptador experimental aceita protocolo **4**, componentes comuns com máscara
única, corpo 1/2 e forma corporal neutra explícita. Resolve IDs pelo catálogo,
rejeita categorias repetidas/especial `0x1000`, verifica enquadramento STRB e
reserva capacidades por `ResultSizeFor`, que permanece C++. Usa os blobs
comprimidos originais, copiados para memória privada; não há inflação prévia
substituindo o stream original. Materiais são fornecidos explicitamente.

- `0x81965FD0`: inicializa o contexto privado de transformação.
- **`0x81967628`**: monta o resultado agregado e os cursores CPU/GPU. Cabeçalho
  de 20 bytes: ponteiro do esqueleto, objeto especial, quantidade, referências
  (13 × 32 bytes), descritores (13 × 52 bytes). Reserva 6928 bytes para esqueleto.
- **`0x81966FC8` → `0x819669A0` → `0x81966450`**: valida documento/categoria/corpo,
  acrescenta referência e descritor, decodifica malhas/texturas via stream LDI
  original e aplica materiais usando callbacks reais do jogo. O aumento de
  contagem feito pelo AOT antes do worker permanece privado em caso de erro.
- `0x819672F8`: conexão da segunda fase para assets que sinalizem campo 4,
  executada somente depois de todos os descritores estarem presentes. **Não
  exercitada com asset real nesta etapa**: nenhum blob do catálogo inspecionado
  contém simultaneamente campos 3 e 4. A ordem/erros/cancelamento dessa fase foram
  testados com backend sintético; assets exclusivamente modificadores continuam
  fora do perfil aceito.
- **`0x819660A0` → `0x81975C00`**, seguido de **`0x81966348`**: cria o esqueleto
  com forma neutra e remove descritores vazios antes da publicação.

Somente durante cada passo AOT, são instalados protocolo 4 em `0x81B4B238` e a
tabela real de callbacks em `0x81A975CC`; ambos são restaurados por escopo. Isso
é válido para a sonda síncrona na thread do jogo, **não é sincronização suficiente
para um serviço SDK concorrente**. Nenhum gerenciador global ou DllMain foi iniciado.

### Evidência nova

Opção exclusiva `--motocross_xam_aot_probe_aggregate=true`, junto à opção principal
do probe. Execução **`20261013T015915Z-mm-avatars-aggregate-1`**, zero falhas:

- Registros **72 (Power Tee) e 0 (MaleBody)**, nessa ordem, geraram um único
  resultado com **dois descritores e 71 ossos**, HRESULT 0. Seleção diagnóstica:
  materiais 22–24, RGB 0.5/alfa 1; não é o avatar de um usuário.
- Referências/IDs, categorias, contagem, ausência de sobreposição entre assets,
  ponteiros de malhas/texturas e esqueleto dentro dos buffers compartilhados,
  cursores, sentinelas e preservação das entradas conferidos. Não é comprovação
  visual nem nova validação completa dos bytes de malhas/texturas já testados.
- Cancelamento antes da preparação, após um asset e após ambos; destruição
  pendente; liberação repetida; polling após conclusão/falha. Nenhuma saída
  parcial publicada, nenhuma alocação privada remanescente nesses casos.
- GUID alterado **só na cópia privada do segundo asset**: o próprio AOT devolveu
  `0x80004005`, preservado pelo coordenador, descartando também o primeiro asset.
  Protocolo 2 rejeitado antes de alocar/executar; não há fallback silencioso.
- Zero violações da ABI verificada; tabela/protocolo/contexto do jogo restaurados.
  `XamAvatarInitialize` continuou retornando **`0xFFFFFFFF`**. Supervisor encerrou
  após 45 s, com os quatro erros XCTD/devkit e quatro stubs de voz conhecidos.

Teste novo `tests/avatar/assets_request_test.cpp`, alvo
`x360n_avatar_assets_checks`, CTest `avatar.AssetsRequest`: **61 verificações,
zero falhas**. Cobre ordem de aplicação após todos os decodes, erro em cada fase,
cancelamento em cada fronteira, rejeição de sucesso sem buffers, contagem inválida,
backend nulo e propriedade/liberação. Disponível no projeto raiz e no build do
Motocross com `-DX360N_AVATAR_ASSETS_CHECKS=ON`; log local
`.local/aggregate-lifecycle-result.log`. Build incremental final aprovado.
Suítes anteriores de tamanhos, STRB, cores, malhas e imports LDI não foram repetidas.

**Próximo bloqueio concreto:** conectar o serviço C++ ao contrato do export
`XamAvatarGetAssets`/overlapped e a um estado de inicialização realmente funcional,
incluindo concorrência e buffers do chamador. O export continua stub. Ainda faltam
protocolo 2, componente especial `0x1000`, modificadores independentes/campo 4 com
dados reais, forma corporal não neutra e renderização. O sucesso demonstrado é do
resultado privado no perfil acima; não constitui `GetAssets` completo do jogo.

## 14. Servico real e entrada no tutorial (2026-10-13)

Incremento exclusivamente na worktree `x360-avatars`, branch `avatars/xam-aot`.
Opt-in `--motocross_avatar_service=true`; incompatível com o probe diagnóstico.
Não houve integração na main, inicialização de DllMain, app manager, heap global
ou workers originais do XAM. O SDK permanece responsável pelos serviços de console.

### Contrato implementado

- Leitor do catálogo original (`8197D1B0`, `8197D060`) e gerador de metadata
  (`8197FD38`, `8197FDA0`, `8197FF28`) usam workspace privado e montagem somente
  leitura. Initialize copia os cinco callbacks (a tabela do jogo está na pilha),
  abre o catálogo, gera metadata e decodifica/aplica/finaliza um avatar completo
  antes de publicar READY. Falhas reais são propagadas; não existe sucesso forçado.
- `GetAssets` usa os buffers CPU/GPU do chamador; só scratch e cópias dos streams
  são privados. Resultado inclui corpo, cabeça, roupas, cores, forma corporal,
  modificadores de textura e vértices. Finalização original `819660A0/81966348`.
  Release libera temporários e preserva o resultado entregue; falha invalida o header.
- Mutex abrange a operação, troca/restauração dos globals do perfil e callbacks.
  Contexto PPC/FPSCR do chamador é restaurado. Conclusão imediata real pelo SDK
  escreve XOVERLAPPED/evento/APC; erro operacional é preservado em ExtendedError.
  Ainda não é um worker assíncrono concorrente. Shutdown libera o serviço enquanto
  Runtime está vivo; reinicialização/Shutdown durante uso não foi exercitada no jogo.
- `GetManifestLocalUser` retorna ausência real de metadata de perfil. O jogo usa
  seu próprio fallback; isso não cria um avatar persistido de usuário.
- Mipmaps chamam `81978C90` para cada textura, ordenada como no original, com
  callbacks reais do jogo. Retorno falso é erro, inclusive nos níveis pequenos.
  Os alocadores internos `816E69E0/816E6AB0` e nothrow new `816E6B18` encaminham
  ao XMem do SDK; delete `816E6B10` já chama o mesmo free. Assim o codec original
  cria e destrói suas superfícies sem depender do heap global não inicializado.
- `LoadAnimation` tem ponte ao leitor original, mas não foi chamado nestas sessões
  e permanece não validado. `SetCustomAsset` retorna explicitamente não implementado.
  Protocolo 2 e assets especiais `0x1000` seguem sem suporte; não há fallback falso.

### Correção do codegen e evidência nova

O codec BC1 original executa `vavguw`. O patch **0036** emite média arredondada
para cima por lane uint32, com soma uint64 para evitar overflow, incluindo destinos
aliased. O teste novo `sdkcheck.Vavguw` recompila quatro variantes PPC e compara
com identidade independente: **66 560 verificações aprovadas**. Antes do patch,
o mesmo teste falhou por instrução não implementada (exit 3). Nenhuma das suítes
anteriores de STRB, cores, malhas, texturas, LDI ou lifecycle foi repetida.

Runs locais:

- `20261013T021743Z-mm-avatar-service-full-1`: quatro metadata completos, com
  seis descritores/12 registros/seis modificadores por resultado; zero falhas ABI.
- `20261013T025546Z-mm-mipmap-return`: falha real no nível BC1 2×2,
  `81A1CE90 -> 80004005`. A fábrica de superfícies usava o new do heap XAM não
  inicializado. O encaminhamento ao SDK removeu essa falha sem substituir o codec.
- `20261013T030158Z-mm-codec-sdk-new`: `GetAssets` do jogo decodificou 13 registros,
  aplicou sete modificadores, finalizou uma vez, guards intactos. Mipmaps:
  sete texturas, 131 072 bytes, HRESULT 0. Processo permaneceu ativo até o limite
  de 45 s do supervisor, sem crash; timeout não é encerramento normal do jogo.
- `20261013T030314Z-mm-avatar-menu`: interface observada na pista do tutorial,
  contagem regressiva e depois 88 MPH/748 pontos. Objetivos de volta, drift e uma
  manobra aparecem concluídos. Isso confirma gameplay inicial, não conclusão do jogo.

**Limitações visuais observadas:** chão preto e geometria do piloto deformada.
Logs têm fetch constants inválidas, quatro stubs de voz, consultas XCTD ausentes
e tentativa de caminho `media:/Tutorial_2k/...`. A causa de cada defeito gráfico
ainda não foi isolada; não foi ativado bypass de fetch inválido para ocultá-lo.
Save/load, corrida completa, multiplayer e persistência do avatar não aprovados.

Build privado `RelWithDebInfo`: `.local/build/titles/motocross_madness-avatars`.
Executar com o backend xenos, `--mnk_mode=true`, as raízes locais do jogo/usuário,
`--motocross_avatar_service=true`, `--x360n_xam17559_root=<extração 17559>` e
`--x360n_avatar_catalog_17559=<AvatarAssetPack.toc>`. Conteúdo original e arquivos
gerados permanecem locais/ignorados. A correção do gerador exige codegen com
`--ignore-stamp`; após uma alteração de analysis, conferir que o objeto C++ gerado
foi recompilado (a regeneração já exigiu uma segunda passagem incremental).

Sessão de gameplay encerrada com **exit code 0 após 456 534 ms** (7 min 36 s),
sem timeout/crash no relatório do supervisor. O build final após a limpeza de logs
é incremental; não se repetiu o percurso do jogo por uma alteração só de diagnóstico.

Em complemento, o teste novo `avatar.ModifierCounts` exercita 14, 22 e 32 registros (componentes + modificadores) e rejeita 33 antes do backend: 10 verificações aprovadas, executadas separadamente da suíte de lifecycle anterior.

## 15. Integração na main (2026-10-13)

O merge `5ee0f3d` preserva os sete commits `536f5f8`, `582cb16`, `dffa97f`,
`65c7bf1`, `7fbcf94`, `7db63ae` e `1a4608a`, incluindo os experimentos opt-in.
Não houve squash nem reescrita. A base principal `2b4b9b8`, com as mudanças de
distribuição, SDK, supervisor e runtime, foi preservada. A alteração local de
`scripts/diagnostics/window-route.ps1`, pertencente a outra sessão, ficou fora dos commits.

Validação própria da integração, autorizada separadamente dos incrementos acima:

- SDK fixado em `f5337cdc`: os 36 patches foram reaplicados em uma cópia limpa,
  incluindo o SDL na revisão fixada; o aplicador conferiu os hashes finais.
  Os 45 arquivos cobertos pelos patches coincidem com a antiga worktree.
- Build `RelWithDebInfo` novo em `.local/build/integration-avatars`, com saídas
  do SDK também isoladas. Nenhuma DLL foi reutilizada do build de outra worktree.
  Codegen dos quatro títulos reexecutado com o gerador novo e `--ignore-stamp`.
- **104/104 testes aprovados**: 54 `x360n`, 45 `sdkcheck`, três catálogos locais
  de avatar e dois testes de lifecycle/contagem. Incluem LDI, `vavguw`, callbacks,
  supervisor, identidade de títulos, retorno à dashboard e foco SDL.
  A suíte completa upstream não foi repetida; isso não altera os resultados
  históricos nem a falha conhecida de `unit.Depfile` registrada em STATUS.
- `motocross_avatar_service` e todos os probes continuam `false` por padrão.
  Initialize só publica READY depois da abertura e montagem reais. Os limites
  funcionais e visuais da seção 14 permanecem explícitos.
- Quatro títulos compilados com o SDK novo: Iruka, Castle Crashers, dashboard
  17559 (incluindo bootanim) e Motocross Madness. Os cinco probes originais
  passaram **128 verificações, zero falhas**: base 32, malhas/texturas 10,
  LDI 11, agregado 16 e serviço completo 59. Runs `20261013T034335Z` a
  `20261013T034546Z`, sufixos `integration-mm-*`. O serviço normal fica desligado
  nestes probes; sua falha de Initialize ao retomar o jogo é esperada.
- Iruka L1–L5: `20261013T034230Z-integration-iruka`, 60 s sem crash, erros ou
  stubs, encerrado pelo timeout previsto. Capturas pela API Sky: 374 cores
  quantizadas, diferença média 9,801 (limiares 32 e 0,5).
- Motocross: `20261013T034627Z-integration-mm-live`, tutorial em movimento a
  36 MPH, Initialize/GetAssets reais com HRESULT 0 e sete texturas/131 072 bytes
  de mipmaps. Janela fechada normalmente após 46,406 s, exit 0. Quatro stubs de
  voz e seis erros de recursos/heap, além do chão preto e piloto deformado,
  permanecem limitações conhecidas; não se declara compatibilidade completa.
- Ciclo 17559 → Castle Crashers → 17559 confirmado na mesma janela. Intro
  original retornou 0; My Games abriu o jogo e seu menu. O usuário executou
  Return to Arcade, porque os toques curtos da automação não moviam esse menu.
  Castle Crashers (`20261013T034922Z-session-1-castle_crashers`) encerrou com
  exit 0 após 98,318 s, zero erros/stubs/críticas. A volta
  (`20261013T035100Z-session-2-dash_17559`) recebeu flags `0x100`, motivo 23 e
  1 020 bytes de launch data; menu reapareceu sem repetir a intro. Os stubs
  conhecidos da dashboard permanecem. Não houve medição audiovisual contra
  console nem ampliação da validação de save/load ou gameplay de Castle Crashers.

Logs e fixtures privados: `.local/evidence/integration-avatars/`; dados de usuário e
caches desta validação: `.local/user/integration-avatars/`. Originais, AOT
gerado, builds e logs não fazem parte dos commits.

### Encerramento da worktree

Após esses resultados, a branch foi confirmada como ancestral da main e não
havia processo de build/jogo ou outra sessão ativa usando seus diretórios.
As diferenças pendentes eram finais de linha e o SDK já coberto pelos patches;
não havia código funcional exclusivo sem commit.

Todo o diretório foi movido, sem exclusão recursiva, para o arquivo local ignorado
`.local/archive/preserved/x360-avatars-20261013/`. Os inventários de 25 095 entradas
(arquivos, diretórios e links), incluindo tamanhos e destinos das junções,
coincidem antes/depois. Os 23 057 arquivos regulares (3 564 848 724 bytes),
logs, dados de usuário, experimentos, AOT e SDK privado permanecem recuperáveis.
O marcador `.git` virou `.git.worktree-archived`, para que essa cópia não seja
confundida com uma worktree ativa. As duas junções `.local/inputs` e
`.local/toolchains` continuam apontando para os dados reais da main, que não
foram removidos. O SDK era um diretório físico independente, também preservado.

`git worktree remove` retirou o registro do caminho antigo, e
`git branch -d avatars/xam-aot` removeu a branch sem forçar. A inspeção final de
metadados não apontou entradas obsoletas. A worktree de distribuição ficou no
mesmo caminho e commit `2b4b9b8`. Nenhum push ou release foi realizado.

A main volta a ser a referência de desenvolvimento. O SDK continua aparecendo
modificado pelos patches/materialização esperados. A alteração de outra sessão
em `scripts/diagnostics/window-route.ps1` permanece sem commit e com SHA-256
`c307f4bac6ec049967c445d7672c4357d009419303a372a77ae10cba67863201` inalterado.
O bundle `.local/evidence/integration-avatars/avatars-history.bundle` conserva também
as referências anteriores à integração. Nenhuma pendência funcional exclusiva
ficou apenas na branch eliminada.

## 16. Construtores estáticos do XAM 17559 (2026-10-16)

O serviço AOT não executa o entry point do `xam.xex`, então os construtores
estáticos C++ (tabela CRT em `0x81A80010`) não rodavam. Os vetores de escala por
eixo usados na dequantização do esqueleto (`0x81A975B4`, `0x81A975C0`) ficavam
com o valor da imagem, (2, 0, 0): todos os ossos perdiam Y/Z e rotação, e o piloto
animado do Motocross Madness se desfazia em estilhaços. O serviço agora executa,
após carregar o módulo, exatamente o bloco de 9 construtores de constantes do
avatar (`0x81A801BC`, `81A70E60`…`81A71008`), conferindo cada entrada da tabela.
Construtores de objetos/`atexit` seguem sem execução. Detalhes e evidência em
[Motocross Madness](reports/titles/validation-motocross-madness.md#piloto-deformado-causa-e-correção).

## 17. Serviço padrão, ligado ao perfil da dashboard 17559 (2026-10-16)

O serviço deixou de ser exclusivo do Motocross Madness e não depende mais de flags
manuais. Fatos desta mudança; o que não foi executado está marcado no fim.

### Código compartilhado

- `titles/common/avatar/avatar_service_17559.cpp` (antes
  `titles/motocross_madness/src/avatar_service.cpp`). O bloco verificado de
  construtores estáticos (`xam17559_static_init.h`, §16) continua idêntico e é
  executado antes do uso.
- `titles/common/avatar/avatar_service_17559.cmake`:
  `x360n_add_avatar_service_17559(<título>)` adiciona só o serviço; o título
  declara `avatar_service` no manifesto e não precisa embutir um XAM próprio.
- Cvars: `x360n_avatar_service` (**padrão `true`**), `x360n_avatar_xam_17559` e
  `x360n_avatar_catalog_17559` (caminhos fornecidos pelo supervisor). A antiga
  `motocross_avatar_service` foi removida. `x360n_xam17559_root` permanece só para
  os probes diagnósticos do Motocross; com `--motocross_xam_aot_probe=true` o probe
  desliga o serviço, como antes (o `Initialize` posterior do jogo falha nesse modo).
- **Uma imagem XAM por processo, compartilhada com o Guide.** A primeira versão
  carregava uma segunda cópia (`x360nsys:\xam.xex`, DLL do título) em `0x815F0000`;
  depois o Guide não conseguia carregar a sua (`Unable to allocate XEX memory at
  815F0000-00610000`, run `20261016T230448Z-session-1-motocross_madness`, pedidos
  `00021015` com `80070490`). Agora o serviço usa a imagem que o host de UI da
  dashboard associa (`sys17559:\system\xam_ui.xex`, `dash_17559_xam_ui.dll`,
  exportada por `X360nSystemUiXamModule` e lida por `x360n::SystemXamModulePath`),
  e só a aceita se for o mesmo arquivo que o supervisor verificou. O Guide aceita
  a imagem já carregada e faz sua inicialização de UI uma única vez (falha não é
  repetida, como antes). Nessa DLL valem os encaminhamentos de alocação do Guide
  (`system_ui_memory.cpp`), não os do antigo DLL do título.
- O XAM embutido no Motocross (`[[modules]] x360nsys:\xam.xex`, com
  `src/xam_avatar_memory.cpp`) ficou só para os probes diagnósticos.
- Sem os dois caminhos, com `x360n_dashboard_version` diferente de `2.0.17559.0`
  ou sem a imagem do host, `Initialize` registra o erro e retorna `80070002`; não
  procura arquivos nem usa outra versão. O supervisor também recusa avatar quando
  a dashboard não tem `system_ui_host`.

### Registro e recusa antes do runtime

- `titles/dash_17559/preparation.toml`: `[avatar]` com `version = "2.0.17559.0"`,
  `xam` = `system/xam_ui.xex` (SHA-256 `78b3dbd8…f828467`, procedência
  `SystemUpdate_17559_USB.zip`, `$SystemUpdate/su20076000_00000000`,
  `$flash_xam.xex`) e `catalog` = `avatar/AvatarAssetPack.toc` (SHA-256
  `a8c90b3d…832daa8`, `$SystemUpdate/FFFE07DF00000002`, pacote "AvatarAsset"
  `0x20449700`). Os hashes vêm das entradas `[[files]]`; o catálogo é `optional`:
  falta dele não impede a dashboard, mas um arquivo diferente é apontado.
- O importador do desktop aceita `[[files]] optional = true`: copia o arquivo quando
  presente na pasta selecionada e não exige quando ausente.
- Manifesto do título: `avatar_service = "2.0.17559.0"` e `avatar_xam_sha256`
  (o XAM compilado no título). `x360n session` chama `AvatarServiceArguments`
  (`src/supervisor/session_profile.cpp`) a cada transição e recusa o título, antes
  de iniciá-lo, com mensagem que nomeia versão, arquivo, SHA-256 e procedência:
  dashboard de outra versão (ou sessão iniciada no próprio título), dashboard sem
  `[avatar]`, XAM do perfil diferente do compilado no título, arquivo ausente,
  divergente ou fora da raiz da dashboard. Perfis não podem passar `--x360n_avatar_*`.
- Motocross Madness ganhou registro de biblioteca (`preparation.toml`, `[library]`
  `58410B0F`/`000D0000`, inventário `assets.sha256` com 54 assets conferidos) e
  `[x360n] game_data_root`, porque `[project] game_root` precisa ser a raiz VFS do
  codegen (`.local/inputs`) para incluir o `xam.xex`. Assim o jogo aparece na My Games
  da 17559 depois de preparado pelo aplicativo.

### Testes e o que não foi verificado

- `x360n.avatar service files belong to the session dashboard version and bytes`:
  sucesso e recusas (versão, `[avatar]` ausente, XAM divergente, catálogo ausente e
  alterado, registro sem hash, fora da raiz, argumentos reservados).
- `x360n.session hands only its dashboard's verified avatar files to an avatar title`:
  supervisor real com participante sintético; a dashboard não recebe argumentos, o
  título recebe os dois caminhos; nas recusas o título não é iniciado (exit 3).
- `local.Motocross Madness receives the 17559 dashboard avatar files` e `…is refused
  by other dashboard versions`: perfis reais do repositório e arquivos do usuário.
- `desktop.catalog` (`optional dashboard components may be absent but never differ`).
- O `AvatarAssetPack.toc` do usuário foi copiado, com hash conferido, para
  `.local/inputs/dashboards/17559/runtime/avatar/`, como faria a importação guiada.
- `local.motocross.shared-xam` (`tests/local/motocross-shared-xam.ps1`): execução
  limitada pelo `x360n run`, com o probe opcional do Guide, exigindo READY do serviço,
  inicialização da UI do Guide depois dele e nenhuma segunda carga do XAM.
- Execução `20261016T233404Z-mm-shared-xam` (builds `dash_17559` e
  `motocross_madness` novos, 55 s até o limite, sem entrada): o serviço usou
  `sys17559:\system\xam_ui.xex`, `Initialize -> 00000000` e READY aos 0,7 s; o Guide
  foi pedido aos 30 s, completou toda a inicialização original (dispositivo,
  compositor, bootstrap XUI, skin, localização, HUD, cena) e compôs 600+ quadros.
  Nenhuma segunda carga de XEX nem erro do serviço; o único erro é o
  `devkit:\senoverride\…\transport.ini` já conhecido. O verificador (com
  `-ExistingLog`) aprova essa execução e reprova a anterior,
  `20261016T230448Z-session-1-motocross_madness`, que tinha o conflito.
- **Não verificado:** `GetAssets`, mipmaps e o piloto não foram exercitados nessa
  execução curta (tela de título, sem gameplay); dependem de jogar até a pista.
  Títulos AOT genéricos importados ainda não recebem o serviço.
