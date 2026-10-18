# Destinos finitos de branches PPC sem tamanho inventado de tabela — 17–18/10/2026

Continuação de [dependências AOT](aot-dependencies-2026-10-17.md).
SDK preservado em `f5337cdc947ff6d4c4196737e2c807a48f2a1fc2`, LLVM 22.1.8,
90 patches ativos, até 0091; 0073 permanece retirado. Sem alteração no renderer,
bytes originais, hints dos jogos ou isolamento das versões de dashboard.
Somente commits locais; sem push, configuração Release ou publicação.

## Reprodução e causa

A compilação focada do objeto `RuntimeHost/...recomp.62.cpp.obj` reproduziu os
seis erros de labels ausentes, exit 1 em 9 s. A evidência inicial e o SDK aplicado
anterior ficaram preservados antes das mudanças. Não se aceitou um recibo parcial.

Em `0x82471774`, o original executa `bctr` sobre o resultado de:

```text
lwz r11,272(r3); addi r11,r11,-1
lis/addi r12,0x822742B8; lbzx r0,r12,r11
rlwinm r0,r0,2,0,29
lis/addi r12,0x82471778; add r12,r12,r0; mtctr r12
```

Não existe limite local demonstrado para o índice carregado do objeto. A análise
anterior lia até 512 bytes como slots de uma tabela. Dados vizinhos também
produziam endereços executáveis, fazendo a descoberta atravessar o `.pdata`
`[0x82471730,0x82471880)` e importar fragmentos da função seguinte. Esses
fragmentos tinham desvios para blocos não emitidos no primeiro corpo. O emissor
ainda podia substituir o resultado da análise por uma segunda heurística tardia.
Zero erros estáticos, portanto, não garantia C++ compilável.

## Correção genérica

Patch 0090 reconhece uma cadeia contígua de definições e usos: `lbzx`,
`rlwinm` opcional, base constante, `add`, `mtctr`, `bctr`, permitindo nops.
Confere os registradores; uma sobrescrita ou uma carga de outra largura impede
esse reconhecimento. Os 256 valores possíveis do byte, a rotação/máscara PPC
exatas, o wrap de 32 bits e a limpeza dos dois bits inferiores de `bctr`
determinam um domínio finito de destinos. Isso **não determina nem presume o
número de entradas da tabela**. Nenhum byte da tabela é lido por esse caminho
da análise e nenhuma tabela manual é criada.

Somente destinos locais na região de código, antes da próxima entrada
conhecida, alimentam a descoberta de blocos. Casos fora do `.pdata` podem
estender a função pelo intervalo livre anterior à próxima entrada, preservando
o contrato existente de blocos fora de linha. Destinos de chamadas diretas são
indexados na passagem de decodificação já existente, protegendo também funções
cujo chamador entra depois no grafo. Chamadas de captura de PC (`bl` para a
palavra seguinte) não viram fronteiras, e chamadas dentro do intervalo declarado
não dividem a metadata. Sucessores seguem até seus terminadores e a mesma
barreira, inclusive além do maior destino do domínio finito. O C++ conserva a leitura original e
toda a aritmética e faz `switch (CTR & ~3u)` sobre esses destinos. O caminho
default usa o resolvedor indireto existente, como tail dispatch, preservando a
recusa de endereços não registrados. Não se cria uma entrada artificial dentro
de outra função. Tabelas manuais continuam prioritárias; resultados da análise
agora precedem a heurística tardia do emissor. Outros padrões mantêm a análise
anterior. Não há JIT, interpretador ou scanner novo no runtime.

A análise inicial corrigida do RuntimeHost teve zero erros, 19.633 funções e
1.699.242 palavras cobertas. O corpo `0x82471730` voltou a ter exatamente 336
bytes, terminando em `0x82471880`; `0x82471E48` pertence à função seguinte.
Essas contagens medem cobertura estática, não execução nem desempenho.

## Testes sintéticos e patches

- Red: fixture própria reproduziu expansão indevida para o corpo vizinho e
  despacho pelo índice, com seis asserções reprovadas.
- Green final: oito casos / 271 asserções de codegen, limites, thunks e tabelas
  manuais. Incluem registradores diferentes, `add` comutado, imediato negativo,
  máscara circular, byte sem escala e cadeias sobrescritas.
- A revisão de compatibilidade acrescentou uma fixture com casos fora do
  `.pdata`. Ela reprovou três asserções na primeira implementação, que limitava
  demais a descoberta. O ajuste usa a próxima entrada conhecida como barreira;
  o teste final confere tanto `.pdata` completo quanto incompleto. O primeiro
  build integral foi interrompido deliberadamente em 326 s para incorporar esse
  teste e refazer a preparação, sem publicar recibo nem avançar para os jogos.
- A segunda auditoria ampliada encontrou dois labels ausentes em `0x823DBDE0`:
  o corpo vizinho `0x823DBF18`, chamado por `bl`, era registrado somente numa
  iteração posterior. A fixture reproduziu quatro asserções reprovadas; o índice
  de chamadas diretas tornou a barreira independente da ordem de descoberta.
  Outra fixture reprovou duas asserções por truncar o caminho após o maior
  destino possível; a descoberta agora segue o corpo até o terminador. Ambas
  passaram no conjunto final, incluindo a exclusão da captura de PC.
- `x360n_ctr_dispatch_native`, opt-in e fora de ALL: compila a saída real do
  pipeline para um XEX sintético e executa **3.072 chamadas** com o Runtime e o
  resolvedor host reais. Confere casos locais, saída para outra função, LR,
  registrador não volátil, CTR e alterações da tabela após AOT, inclusive índices
  de 512 a 1023. Nenhum stub substitui o resolvedor.
- O primeiro build do harness identificou uso incorreto do macro de status;
  a primeira execução identificou proteção de escrita da `.rdata` sintética.
  Ambos foram corrigidos no teste; a imagem sintética recebe permissão de escrita
  apenas para testar mutações posteriores ao AOT. Falhas e pilha preservadas.
- Replay em LF reproduziu os seis hashes finais do patch 0090 a partir dos fontes
  anteriores. `apply-sdk-patches.ps1 -Check` confere os patches e seus hashes finais.

O primeiro prepare integral encontrou também o limite de linha de comando de
`cmd.exe`: a limpeza de todas as regras de compilação/link dos 32 módulos excedia
8191 caracteres. `Clear-NinjaNativeProducts` divide as regras em lotes limitados,
limpa todas e propaga qualquer falha. O teste com Ninja real removeu 180 objetos,
preservou o carimbo customizado AOT e recusou um grafo inválido. A publicação
atômica e os contratos PowerShell dos recibos também passaram.

Os contratos C++ de preparação e sessão passaram em 22 casos / 33.121 asserções,
incluindo identidade de artefatos, dependências, isolamento e persistência de dados.

## Bloqueio de link já existente e contrato de câmera ausente

O primeiro build com o codegen final compilou todos os corpos C++ e chegou ao
link do RuntimeHost, mas parou em oito imports `XUsbcam*` (414 s no prepare).
O SDK reconhecia seus nomes, mas excluía o próprio fonte do kernel; o problema
já estava registrado em `docs/architecture.md` §7.14. Não se criaram definições
locais para Charlie, nem se habilitou o fonte upstream sem revisar sua semântica:
ele retornava sucesso fictício em Create e tinha nove stubs sem resultado.

Patch 0091 compila os 11 exports existentes e generaliza o contrato já demonstrado
em `titles/common/camera_state.cpp`: estado 0 (desconectado), criação com
`X_STATUS_NOT_IMPLEMENTED`, sem handle, alocação ou escrita de outputs. Operações
sem implementação/assinatura de retorno estabelecida abortam explicitamente com
nome e LR, em vez de retornar sucesso ou um r3 arbitrário. Os overrides antigos
dos perfis permanecem, com o mesmo comportamento. Não há suporte novo a captura,
consulta à câmera do host ou uso de recursos originais de outra dashboard.

O teste liga os 11 exports reais sem shims de título, verifica buffers e
registradores e executa nove subprocessos limitados a 5 s: todos abortaram com
exit 3; nenhum retornou ao chamador. O teste existente de câmera da dashboard e
o de entrypoint passaram em dois casos / 26 asserções.

## Evidência local

Raiz: `.local/evidence/aot-ctr-dispatch-2026-10-17/`. Inclui fontes anteriores,
lock anterior, recibos anteriores, logs red/green, replay e hashes, execução nativa,
falhas iniciais e inventários. Os 983 arquivos do Charlie foram conferidos contra
o catálogo antes das sessões: zero divergências. Cópias preservadas de 22 arquivos
de perfis, saves, cabeçalhos e cache/catálogo monitorados somam 7.532.637 bytes.

Build, recibo válido, carregamento pelo My Games e gameplay são evidências
distintas. Não se afirma ganho de FPS ou performance: esta etapa corrige análise
e geração, sem benchmark em condições equivalentes.

Commits de implementação: `bbb2a0c` (despacho PPC), `4ff91c9` (limpeza multimódulo),
`921c628` (casos fora do `.pdata`), `430e510` (chamadas futuras e sucessores)
e `6d30891` (contrato de câmera ausente).

## Preparação completa do Charlie

A preparação final concluiu em 470,9 s, com oito jobs. O codegen dos 32
binários reanalisou normalmente, sem `--force`: 540 arquivos inalterados na
repetição após o ajuste de exports. Auditoria dos 157 arquivos de corpos C++:
27.893 funções emitidas, 129 despachos por CTR, nenhum label ausente e nenhum
corpo vazio ou chamada estática pendente. O build nativo concluiu em 165 s.

O recibo novo certifica 3.463 inputs, 35 artefatos e os três arquivos do produtor;
o marcador de preparação incompleta foi removido pelo driver. SHA-256 do EXE:
`fe429010a9016f8531cfd61d0e8f6e0b361156295c4510112c522f34b80eaa8d`.
SHA-256 do recibo:
`db86d0a1f27fadaca564e6207c360039b26fefe889653ffdb34372b1e5b1a524`.

Probes de registro dos DLLs reais, sem executar guest, passaram:

| Módulo | Mappings | Base / tamanho | Entrada original |
|---|---:|---|---|
| HostLoader | 1.461 | `82120000 / 110000` | `82163C60` |
| RuntimeHost | 19.632 | `82230000 / 8F0000` | `82376AE0` |

Nos dois módulos a entrada pertence ao DLL nativo carregado e o modo
`host_function_table` é true. Layout e SHA do original foram conferidos. Isso
comprova build, identidade e registro; não comprova DllMain, execução XNA ou gameplay.

Repetição dos testes focados e da preparação:

```powershell
scripts/devshell.cmd cmake --build .local/build/win-amd64 --config Debug --target x360n_sdk_checks x360n_ctr_dispatch_native x360n_usbcam_unavailable_checks
.local/build/win-amd64/tests/Debug/x360n_sdk_checks.exe '[ctr-dispatch],[aot-discovery],[gapfill]'
.local/build/win-amd64/tests/Debug/x360n_ctr_dispatch_native.exe
powershell -NoProfile -File tests/sdkcheck/usbcam_unavailable_test.ps1 -Executable .local/build/win-amd64/tests/Debug/x360n_usbcam_unavailable_checks.exe
powershell -NoProfile -File scripts/build-title.ps1 -Title g0a4c6253b34297dc971555c160fb8affde66f554296bd83bae609a48d5f5b47
```

Os títulos usam o preset existente `RelWithDebInfo`; supervisor e testes são
Debug. Nenhuma configuração Release foi compilada. Tempos de preparação incluem
clean, configuração, compilação e hashes, com cargas/limites diferentes; não são
um comparativo de performance.

## Iruka retirado do escopo pelo mantenedor

A preparação encontrou ausência de `games/iruka/content/default.xex` e recusou
o codegen, sem recibo novo (213,4 s, após recompilar o produtor). O mantenedor
confirmou em 18/10 que removeu o input intencionalmente por ser uma demonstração,
não um jogo comercial. O input não foi restaurado, nenhum caminho alternativo
foi usado pelo runtime e a regressão visual de Iruka não foi executada. A falha
de preparação e o marcador de incompleto foram preservados; não é uma regressão
demonstrada do codegen. Os builds restantes seguiram sem repetir Charlie.

## Entrega do desktop da main para teste manual

Em 18/10 o mantenedor pediu o launcher da main para testar o próprio acervo e
explicitou que não deseja executar todos os jogos a cada alteração. Isso
substitui a próxima bateria visual automática: nenhuma sessão de Charlie,
Moto, Castle ou dashboard foi iniciada nesta etapa. Os resultados de build e
recibos abaixo não se convertem em aceite de gameplay.

`scripts/start-desktop.cmd` abre o frontend Debug deste checkout com o ambiente
correto. Build por `devshell.cmd`, concluído em 19 s. SHA-256 do desktop:
`6b2947b6133006e236d0b2b16feef3e6b0c20b734c1397689b9224e216bb095f`.
O cadastro de conteúdo já situado em `.local/inputs/games/` passou a verificar
e reutilizar os diretórios locais, evitando duplicar discos. Caminhos externos
continuam sendo copiados; links e inventários divergentes mantêm a recusa ou
o isolamento já existente. Testes de importação: 12 casos / 182 asserções.
O snapshot opt-in do catálogo usa `Inspect` real e registra também as falhas.

Há dez pastas, nove com XEX de Xbox 360. A décima,
`need_for_speed_underground_2/disc-1`, contém `default.xbe`, do Xbox original:
não pode ser preparada pelo pipeline XEX/PowerPC. Não se adicionou emulador,
stub ou conversão fictícia. Os discos 1 e 2 do Forza Motorsport 4 permanecem
separados; enumerar o disco 1 não demonstra troca/instalação do disco 2.

A conferência final dos 90 patches passou. Antes do cadastro desta coleção,
os 22 arquivos preservados de catálogo, perfis, saves, headers e cache estavam
inalterados. A configuração gráfica também manteve SHA-256
`2386750e75f4bf1de1df43a323d2c254add321d18e20cc54254195dd01d9d085`.
Backups e evidências permanecem em
`.local/evidence/aot-ctr-dispatch-2026-10-17/`.

Motocross concluiu a preparação em 969,0 s com quatro jobs; o build nativo
levou 507 s. Recibo novo: 3.623 inputs, seis artefatos e três arquivos do
produtor, sem marcador de incompleto. SHA-256 do EXE:
`c808511aedc58ba78b487cbecf71672fb65b63d36418c963d5ce674f3109cd1b`.
Recibo: `cf42723d60881268be9a744116aea24d985165fbd09aa2f9115bac330e1849c3`.
Auditoria dos 325 arquivos gerados: 70.301 funções, 161 despachos por CTR,
nenhum label ausente. Os 162 objetos PPC do Charlie e os 330 do Motocross
mantêm `-fno-strict-aliasing` e `-ffp-model=strict` no grafo de compilação.
O `doctor` do supervisor Debug terminou com zero falhas e 106 avisos,
incluindo os patches locais/materialização já esperados; não é um log sem avisos.

Castle concluiu em 361,4 s com oito jobs (66 s de build nativo). Recibo:
2.973 inputs, cinco artefatos e três arquivos do produtor. SHA-256 do EXE:
`480b53e22fd1e125ce5922343b9478ae6e7df319b5a8f0310419716eee1952d6`;
recibo `edfbcb3df78d619f625c0a56a8c627bba18987e10a86e01648a39d62a4e1f014`.
Foram auditadas 3.715 funções em 17 arquivos,
14 despachos por CTR, nenhum label ausente; 21 objetos PPC com ambos os flags.

O primeiro prepare da 17559 passou em 590,6 s (227 s nativos), mas a auditoria
adicional encontrou seis unidades de compilação, nos módulos signin,
createprofile, vk e hud, sem o contrato PPC. Os targets são criados a partir de
invocações separadas de codegen e não entram nos inventários de fontes do SDK
usados pelo seletor automático. Não se alterou o seletor compartilhado nem se
invalidaram os demais títulos: os quatro targets agora ligam privadamente
`x360n_ppc_options`, o contrato reutilizável já existente. A nova configuração
confere os 210 objetos sem flags ausentes. Build, recibo, CMake anterior e a
auditoria reprovada ficaram em `dash-before-ppc-contract/`; uma preparação nova
refaz o vínculo entre fontes, opções e artefatos.

A auditoria de labels da 17559 incluiu os módulos auxiliares: 206 arquivos,
62.385 funções, 167 despachos por CTR e nenhum label ausente. O atalho
`x360-native (main).lnk`, na Área de Trabalho, aponta ao desktop Debug local e
passa `--root` para este checkout. A abertura real da janela foi observada;
nenhum jogo foi aberto. Não houve inspeção de gameplay nem sessão My Games.

## Fechamento do código e pendências — 18/10

A preparação final da 17559 com o contrato PPC concluiu em 601,7 s, oito jobs,
233 s de build nativo: 3.453 inputs, 13 artefatos e três arquivos do produtor.
EXE `8cd261cce61cb72b8b247984ed4eb4d396d16a4feac8a870ad3729eef3bc5008`;
recibo `f0c9dd272c44e57cd07ef9011c09ad92f2bb80d28f94d79faab06d7fbaae1c43`.
O snapshot final de preparação aprovou Charlie, Moto, Castle e 17559. Isso
certifica os produtos AOT, separadamente da instalação na biblioteca original.

O mantenedor reproduziu uma falha em `tools` pelo desktop. O diagnóstico
`logs/desktop/23651359/tools.error.log` contém `Environment in use`: a tentativa
coincidiu com a preparação externa ainda ativa. O frontend escrevia o marcador
de título incompleto antes de adquirir as travas e também no catch de qualquer
erro. Essa escrita podia invalidar um produto válido ou alterar o marcador de
outro build. O frontend agora deixa a criação/diagnóstico desse marcador ao
driver `build-title.ps1`, que possui as travas; erros de ferramentas, importação
e registro ficam nos logs da operação e nas verificações da biblioteca.

O frontend corrigido foi configurado e compilado em Debug na árvore
`desktop-only` (9,5 / 51,7 s), sem fechar a janela aberta pelo mantenedor.
SHA-256: `128e08236673f2c9b1c96178eea3f818463459a58a29ea776f32b0a0cb58defd`.
O atalho da Área de Trabalho e `scripts/start-desktop.cmd` apontam primeiro a
essa árvore. A janela anterior não foi substituída nem reaberta automaticamente.
Após o Escape físico que interrompeu Computer Use, não houve nova automação de UI.

A fila de novas preparações foi encerrada para permitir os testes manuais.
Castle, Charlie, Forza Horizon e Forza Horizon 2 tiveram cadastro verificado;
Forza Horizon 2 ficou sem geração/build novo. As operações seguintes receberam
estado **adiado**, código 125 do harness, sem iniciar processos. Não são falhas
de compilação demonstradas. Moto já possuía perfil/build válido. Os demais
XEX presentes e a execução dos dez diretórios não foram certificados.

A tentativa final de `PrepareLibrary` real, pelo teste opt-in
`[.local-prepared]`, confirmou os seis pré-requisitos de preparação e falhou
na aquisição da trava exclusiva do ambiente: seis asserções aprovadas, uma
falha por `Environment busy or not writable`, exit 42 em 21,4 s. A abertura
exclusiva do arquivo de trava também confirmou compartilhamento ocupado.
Nenhuma trava foi removida à força, nenhum recibo foi editado e o registro do
Charlie na My Games **permanece pendente**. Não se declara a tarefa inteira
concluída nem os dez jogos prontos para executar.

Preservação final: dos 22 arquivos acompanhados, somente o JSON de catálogo
do Charlie mudou durante o recadastro verificado. Perfis, saves, headers e cache
acompanhados ficaram idênticos; nenhuma ausência foi encontrada. O backup
anterior permanece disponível. A configuração gráfica manteve o mesmo hash.
Sem push, Release ou alteração no renderer. Commits registram o código validado
e os limites efetivamente observados.
