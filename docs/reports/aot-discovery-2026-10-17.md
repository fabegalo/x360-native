# AOT-Discovery: limites de thunks virtuais (17/10/2026)

Incremento genérico sobre ReXGlue v0.10.0, pin
`f5337cdc947ff6d4c4196737e2c807a48f2a1fc2`, LLVM 22.1.8.
Contexto: [handoff](../research/recomp-handoff.md). O pedido desta implementação
autoriza a investigação AOT; não houve sessão visual, execução de jogo, mudança
de renderer, publicação ou alteração dos dados dos títulos.

## Auditoria antes de alterar

O pipeline ativo é `Register → Scan → Discover → GapFill → Merge → Validate`.
`Register` inclui imports, helpers, configuração, `.pdata` e handlers de exceção.
`Discover` usa `discoverBlocks(DecodedBinary&)`, expande chamadas diretas `bl`
iterativamente e passa os limites originais e switches configurados ao scanner.
Não se deve acrescentar um scanner paralelo de funções ou switches.

`function_scanner.cpp` já reconhece tabelas absolutas (`lwzx`), offsets de bytes
(`lbzx`, com/sem deslocamento) e halfwords (`lhzx`). Há dois caminhos: descoberta
com instruções previamente decodificadas e `FunctionScanner::detect_jump_table`
usado como fallback tardio pelo emissor em `function_graph.cpp`. Este incremento
não altera esses caminhos, os limites das tabelas nem suas listas de alvos.
As tabelas manuais de 16 slots de Castle/Motocross continuam necessárias e
autoritativas; limites de tabelas sem metadata não podem ser inferidos pela
proximidade de outra tabela.

`VTableScanner` procura Complete Object Locators/RTTI na `.rdata`, lê slots
executáveis alinhados e encontra apenas a primeira referência a cada COL.
Não descobre todas as vtables sem RTTI. A varredura `functionPointerScan` permanece
desativada: além dos falsos positivos documentados no SDK, seu comentário sobre
invalidação de registradores não corresponde a uma implementação dessa
invalidação. Endereço formado por `lis/addi` sozinho não comprova função.

`GapFill` separava regiões em `blr`, chamadas de cauda diretas a funções
conhecidas e finais de `.pdata` (patch 0068). Faltava separar thunks virtuais
adjacentes terminados por `bctr`: a primeira sequência encerrava a descoberta,
mas a seguinte permanecia dentro do mesmo segmento especulativo. Isso é
reproduzível sinteticamente e na sequência do Motocross em
`0x829387D0`/`0x829387E0`, sem `.pdata` ou RTTI.

## Mudança e limites de confiança

Patch `0088-virtual-tail-thunk-boundaries.patch`, aplicado pelo mecanismo normal
de patches e verificado pelo lock. O SDK continua no mesmo commit. O hash final
do arquivo compartilhado pelo 0068 foi atualizado, preservando seu patch e o
hash de base do novo patch.

O reconhecimento é deliberadamente estrito, com quatro instruções contíguas
desde o começo do segmento:

```asm
lwz   rT,0(r3)       # rT = r11 ou r12, registrador volátil
lwz   rT,slot(rT)    # slot >= 0 e múltiplo de 4
mtctr rT
bctr                # incondicional, sem link
```

Essa sequência comprova uma transferência indireta sem fall-through, não o
valor do destino. Nenhum endereço de destino é inventado ou convertido em
chamada direta. As novas entradas continuam com autoridade `GAP_FILL`, sujeitas
à descoberta/limpeza/validação existentes. O reconhecimento não prevalece sobre
intervalos declarados por `.pdata` ou configuração, inclusive intervalos
sobrepostos e seus buracos inalcançáveis.

`bctrl`, CTR condicional, loads indexados de switches, registradores divergentes,
slots negativos/desalinhados e registradores não voláteis não satisfazem a regra.
Não se ativou varredura geral de ponteiros, scan indiscriminado de `.rdata`,
heurística de proximidade nem lista de endereços por jogo. A separação usa o
`DecodedBinary` já existente, retirando a decodificação repetida do GapFill.
Não há novo custo no runtime dos jogos.

## Método reproduzível

`x360n_aot_discovery_probe` é um alvo explícito, fora de `ALL` e CTest. Executa
somente o pipeline de análise, exporta funções/autoridades/blocos/tabelas/erros e
consultas de endereço em ordem estável. Usa o mesmo `emitCpp` do writer para
registrar fingerprints XXH3-128 dos corpos selados; isso compara C++ emitido,
não é hash de identidade ou prova de equivalência semântica. A cobertura mede
palavras únicas nos blocos descobertos. Contagens de funções incluem imports;
`non_pdata_entries` exclui imports, mas inclui helpers. Cobertura de análise não
significa que cada palavra seja código válido, que o jogo execute ou que seja jogável.

O coletor guarda SHA-256 dos inputs/configurações, probe, runtime, lock e grafos,
tempo total do processo e seu exit code. Cada processo tem deadline e cleanup.
Recusa sobrescrever evidência. Os manifestos são isolados em `.local/evidence`;
nenhum projeto, hint de produção, codegen de título ou recibo é substituído.
Os originais vêm exclusivamente do acervo local.

```powershell
scripts/devshell.cmd cmake --build .local/build/win-amd64 --config Debug --target x360n_sdk_checks x360n_aot_discovery_probe rexglue
pwsh -NoProfile -File scripts/analysis/measure-aot-discovery.ps1 -Probe <probe-congelado> -Output <diretorio-novo>
pwsh -NoProfile -File scripts/analysis/compare-aot-discovery.ps1 -Before <antes> -After <depois> -Output <delta.json>
```

O corpus usa configuração genérica sem hints. Castle e Motocross também são
comparados com apenas o switch manual original, isolando descoberta de funções
das limitações já conhecidas dos switches. As funções manuais dos títulos
permanecem intactas.

## Charlie Murder: procedência do endereço solicitado

`0x82163C60` é o entry point do **HostLoader.dll**, que contém XEX2 apesar da
extensão. SHA-256:
`f8e0a6ab7eba51c02333a84db88d146e591f25940be0967c970c8a926239b98a`.
Imagem em `0x82120000`; instrução de entrada `b 0x82163B08`, sem `.pdata`.
O launcher `default.xex` e `game.exe.xex` não contêm esse endereço.

A base atual já descobre essa entrada quando **HostLoader.dll** é analisado.
O projeto genérico histórico do Charlie só lista `default.xex`, sem esse módulo.
O erro histórico de execução não prova uma falha de descoberta desse endereço
na revisão atual do scanner. Não se adicionou um endereço fictício ao launcher
nem se certificou o carregamento/ciclo XNA. Essa preparação multimódulo fica
pendente; este incremento não torna Charlie Murder jogável.

O comparador qsort do Iruka, `0x82198CD8`, também já aparece na análise genérica
atual sem o hint manual. Esse resultado preexistente não é ganho do 0088.

## Validação focada

- Red: teste de thunks adjacentes falhou na ausência da segunda função; 70/71
  asserções passaram, 1 falhou, em dois casos. Registro preservado.
- Green final: quatro casos de GapFill, **107 asserções aprovadas**, incluindo
  o teste do 0068, nove near-matches recusados e proteção de metadata/configuração.
- Um quinto caso, executado separadamente: **13 asserções aprovadas**, preservando
  a propriedade dos alvos de um switch manual mesmo com sequência semelhante a
  thunk. Total: **120 asserções em cinco casos distintos**.
- Geração C++ da fixture pelo writer normal, sem `--force`, e compilação sintática
  dos corpos e de sua tabela de registro pelo Clang 22.1.8/`devshell.cmd`, usando
  os includes do build do SDK.
- Verificação dos 87 patches ativos e replay do 0088 sobre sua base, reproduzindo
  exatamente o arquivo final com LF, conforme os atributos do SDK.

O grafo completo do Iruka, incluindo fingerprints dos corpos emitidos, foi
repetido em outro processo e permaneceu idêntico byte a byte (SHA-256
`9c0a43b3a211f35ee2d394dd87f8c2ae97807aaedcd5034dd28551633ff9d0d0`).
O Motocross genérico também foi repetido em processo independente, com igualdade
byte a byte do grafo e fingerprints (SHA-256
`640ce277ae89b57d0e88e870b12dabcc0b8d070e474124ff3e7a8437323b1d93`).
Os grafos do launcher e HostLoader do Charlie também ficaram idênticos antes e
depois. Os dois ganhos do Iruka foram conferidos por desmontagem e xrefs:

| Entrada nova | Bloco novo | Evidência em `.rdata` |
|---|---|---|
| `0x820D6E78` | 16 bytes, thunk virtual depois de outro thunk | `0x82010480`, `0x82010708`, `0x82016EC8` |
| `0x82247F68` | 16 bytes, função folha depois de thunk virtual | `0x82041BE0`, `0x82041C00` |

Os limites acima são os blocos efetivamente descobertos, não o tamanho
provisório do segmento GapFill. Nenhuma dessas entradas tem `.pdata`.

## Cobertura comparada

| Corpus sem hints de função | Funções antes → depois | Palavras cobertas antes → depois | Erros antes → depois |
|---|---:|---:|---:|
| Charlie, launcher | 1.707 → 1.707 | 60.012 → 60.012 | 0 → 0 |
| Charlie, HostLoader.dll | 1.461 → 1.461 | 50.332 → 50.332 | 0 → 0 |
| Iruka | 5.327 → 5.329 | 531.746 → 531.754 | 0 → 0 |
| Motocross | 50.967 → 50.976 | 3.969.223 → 3.969.255 | 5 → 0 |
| Motocross, switch manual | 50.967 → 50.976 | 3.969.223 → 3.969.255 | 5 → 0 |
| Castle | 3.908 → 3.908 | 252.610 → 252.610 | 1 → 1 |
| Castle, switch manual | 3.908 → 3.908 | 252.610 → 252.610 | 1 → 1 |

Ganho demonstrado: duas entradas/32 bytes no Iruka e nove entradas/128
bytes no Motocross, sem mudança dos fingerprints dos corpos previamente emitidos.
No Motocross, duas funções existentes antes pendentes também passaram a ser
emitidas (`0x82933900`, `0x82944B70`), resolvendo os cinco desvios de cauda.
As 648 tabelas/34.157 slots do Motocross genérico permaneceram iguais.

Nenhuma entrada foi removida, nenhum corpo previamente emitido deixou de ser
emitido e todas as tabelas/alvos ficaram idênticos **dentro da mesma configuração**
nos sete pares. Charlie e Castle tiveram grafos inteiros idênticos antes/depois.
No Motocross, `0x82933900` mudou de pendente para selada e seus cinco blocos
contíguos (44 + 8 + 8 + 8 + 8 bytes) foram normalizados num bloco de 76 bytes:
a união dos endereços/instruções não mudou. Essa diferença estrutural foi
revisada, não ignorada pelo comparador.

Castle mantém o `UnresolvedCall 0x820E15E8 from 0x820E0D04`, também com o switch
manual. O alvo segue um adjustor terminado por `b`, fora da regra estrita deste
incremento. Nenhum endereço manual foi removido do título para mascarar essa
limitação. Os switches sem limite continuam sendo uma limitação independente:
a análise genérica atual lê 512 slots no `bctr 0x82C68124` do Motocross e 94 no
`bctr 0x82112C04` do Castle; as configurações manuais fornecem 16 em cada caso.
Ambas as formas foram comparadas antes/depois, sem alteração das listas de alvos.

Todas as nove novas entradas do Motocross foram conferidas na desmontagem.
As cinco que eliminam erros têm os chamadores diretos registrados abaixo; as
demais têm referências de dados explícitas, conferidas numa única varredura de
xrefs do intervalo. Não se usaram esses endereços no algoritmo.

| Entrada | Bloco | Procedência adicional |
|---|---|---|
| `0x82471A08` | 16 bytes, thunk virtual | `.rdata 0x8201C8D4` e outras seis referências |
| `0x82471A18` | 8 bytes, adjustor `r3 -= 12; b` | `.rdata 0x8201C8F4` |
| `0x829387E0` | 16 bytes, thunk virtual | `b` de `0x82944B7C` |
| `0x82938890` | 16 bytes, thunk virtual | `b` de `0x82933948` |
| `0x829388A0` | 16 bytes, thunk virtual | `b` de `0x82933940` |
| `0x829388B0` | 16 bytes, thunk virtual | `b` de `0x82933938` |
| `0x829388C0` | 16 bytes, thunk virtual | `b` de `0x82933930` |
| `0x829FA280` | 16 bytes, thunk virtual | `.rdata 0x820AB2C4` e outras oito referências |
| `0x82AEBA80` | 8 bytes, adjustor `r3 -= 4; b` | `.rdata 0x820FB880`, `0x820FB99C` |

Limite demonstrado: `0x82ACC350` continua sem entrada na análise genérica do
Motocross. Resolver os erros estáticos não certifica todas as chamadas indiretas;
os hints de produção e os switches manuais continuam preservados.

Evidência local: `.local/evidence/aot-discovery/` e
`.local/evidence/logs/aot-discovery-*.log`. A primeira medição diagnóstica do
Motocross atingiu seu limite de 180 s e foi encerrada, sem resultado de cobertura;
não foi tratada como sucesso. A coleta completa usa limite de 600 s por processo.
Uma tentativa de build durante a coleta encontrou a DLL de diagnóstico em uso;
foi repetida após o encerramento, antes de executar testes. Os binários de base
foram então congelados em diretório próprio. Falhas de preparação do comando
avulso de compilação da fixture também permanecem nos registros desta sessão.

O primeiro coletor completo registrava o hash do lock ao terminar. Como a base
foi congelada antes de aplicar o 0088, sua identidade foi corrigida a partir de
`baseline-binary-provenance.json`, capturado antes da aplicação. O metadata
original permanece em `baseline/measurement.initial.json`. O coletor atual
captura as identidades no início; o hash do coletor antigo não foi registrado.

Tempos de coleta incluem carga, análise, emissão de fingerprints e gravação do
grafo. São diagnósticos, com coletas e builds focados parcialmente sobrepostos;
não constituem benchmark A/B de performance nem sustentam ganho de FPS,
startup ou throughput. Não houve teste visual, sessão de jogo ou suíte
completa; perfis, saves e renderers não foram revalidados por este trabalho.

| Processo diagnóstico | Antes (s) | Depois (s) |
|---|---:|---:|
| Charlie, launcher | 1,990 | 1,720 |
| Charlie, HostLoader | 1,455 | 1,451 |
| Iruka | 12,048 | 12,393 |
| Motocross genérico | 385,429 | 414,992 |
| Motocross, switch manual | 404,258 | 442,266 |
| Castle genérico | 6,357 | 5,837 |
| Castle, switch manual | 7,907 | 5,914 |

Os aumentos observados também ficam registrados; não há ganho de tempo de AOT
demonstrado. As repetições de determinismo levaram 15,269 s (Iruka) e 449,242 s
(Motocross), sem variar o resultado. Não atribuir a dispersão ou as diferenças
temporais ao patch sem um benchmark serial equivalente.

Implementação: commit local `3bf4878`. Os arquivos do handoff já existentes e
não rastreados em `docs/research/` foram preservados fora destes commits.
