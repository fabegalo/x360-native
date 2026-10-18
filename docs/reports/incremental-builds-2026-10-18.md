# Builds incrementais por conteúdo — 18/10/2026

Escopo: driver de preparação, recibos, grafo CMake/Ninja e caches existentes do
ReXGlue pinado. Nenhuma sessão de jogo, push ou Release faz parte desta validação.
Compilar não certifica execução do título. Alterações preexistentes do patch 0092,
SDK, testes e relatórios de outros trabalhos foram preservadas.

## Auditoria

O driver anterior já reutilizava uma preparação final se todos os inputs,
artefatos, inventários e revisão do SDK coincidissem com o recibo SHA-256. Uma
divergência, entretanto, executava `ninja -t clean -r` para **todas** as regras
nativas e removia outputs de comandos customizados. Um comentário em um fonte
do host recompilava também o SDK e os módulos AOT inalterados.

O ReXGlue já calcula fingerprints por módulo usando conteúdo, configuração e
versão SDK; `WriteIfChanged` conserva arquivos C++ inalterados. O stamp upstream
não identifica o produtor executável e verifica existência, sem comparar o
conteúdo das saídas. Por isso esse stamp sozinho não é certificado suficiente.
O SDK também grava o minuto atual em `version.h` a cada configure. As duas
configurações incondicionais do driver modificavam esse header e recompilavam
consumidores/gerador mesmo sem alterações relevantes. Agora um checkpoint válido
permite omitir configure somente quando o grafo de inputs CMake, presets incluídos,
manifestos, listas de módulos geradas e globs continuam certificados por conteúdo.
Após codegen, listas novas ou alteradas exigem reconfigure. Mudança de ambiente ou
ferramenta recusa o checkpoint antes dessa decisão.

Não houve edição manual do submódulo nem troca do SDK/LLVM pinados.

## Implementação

- O recibo público permanece no schema 1, com os mesmos contratos e publicação
  atômica. Seu campo opcional `incremental` e seus inputs autenticam um checkpoint
  privado em `<build>/.preparation/incremental.json`.
- O checkpoint é exclusivo do título e diretório. Registra SHA-256 dos produtos,
  assinaturas dos comandos e dependências, bancos Ninja e política de ferramentas,
  scripts, lock e patches. Não há cache global de objetos ou módulos originais.
- As arestas vêm de `ninja -t graph`, os comandos de `compdb`, e os headers
  efetivamente consumidos de `ninja -t deps`. `compdb -x` expande response files;
  flags escondidas nesses arquivos participam das assinaturas. Dependências apenas de ordenação
  não contaminam os fingerprints de compilação.
- Uma alteração de conteúdo invalida o produto consumidor. Corrupção de objeto
  também o invalida, independentemente de tamanho ou timestamp. Após o codegen,
  uma segunda comparação detecta C++/headers gerados que realmente mudaram.
- O ambiente real do devshell, inventários de includes, bibliotecas implícitas e
  recursos ficam em `.preparation/toolchain.json`. Adicionar um header que possa
  mudar a resolução de includes também invalida a preparação. São inventariados
  nomes de arquivos sem filtrar extensão, incluindo diretórios de includes locais
  entre aspas. Valores de ambiente são armazenados como hashes. Bibliotecas
  implícitas alteradas participam das assinaturas de link.
- Todos os outputs do inventário do codegen e seu stamp entram no recibo.
  Produtor diferente ou estado sem procedência exige `--ignore-stamp`; outputs
  divergentes invalidam o stamp do módulo correspondente. Nunca se usa `--force`.
- Checkpoint ausente/truncado, identidade divergente, bancos modificados, mudança
  de política/revisão SDK e preparação interrompida usam reconstrução conservadora.
  Não se confia em intermediários de uma tentativa sem certificação completa.
- Travas de ambiente, diretório e título permanecem. Consultas nativas em buffer
  usam o Ninja subjacente somente sob a mesma posse do driver, evitando pipelines
  PowerShell por linha. Não ignoram a trava para iniciar builds concorrentes.
- Cada tentativa tem logs `x360n-build-<etapa>.<id>.log`. Handles retidos por uma
  tentativa interrompida não obrigam a truncar logs antigos nem impedem novos logs.

O checkpoint só é publicado após um build bem-sucedido. Antes de publicar o
recibo, o driver confere novamente fontes, produtor e scripts. Uma falha retém o
marcador de preparação incompleta; não há edição manual de recibos para contornar
o gate. Originais, perfis, saves, catálogo e isolamento de dashboards não mudam.

## Validação focada

`tests/build_isolation/incremental_test.ps1` cria um projeto CMake/Ninja próprio,
sem conteúdo proprietário e sem executar seu binário. Resultado observado em
PowerShell 5.1 com LLVM 22.1.8:

| Cenário | Objetos invalidados | Resultado |
|---|---:|---|
| Sem alterações | 0/3 | preservados |
| Um fonte com timestamp preservado | 1/3 | só o consumidor |
| Header compartilhado, timestamp preservado | 2/3 | ambos os consumidores |
| Header removido/renomeado | 2/3 | dependência antiga removida com segurança |
| Template/header gerado | 1/3 | comparação após geração |
| Objeto corrompido com timestamp preservado | 1/3 | reconstruído |
| Flags de compilação diferentes | 3/3 | todos os consumidores |
| Flags dentro de response file | — | assinatura muda com o conteúdo expandido |
| CMake/preset incluído com mesmo timestamp; nova lista CMake gerada | — | configure exigido |
| Biblioteca implícita alterada | 0/3 | apenas link invalidado |
| Manifesto nativo alterado | 0/3 | apenas link invalidado |
| Entrada/produto com caminho acima de MAX_PATH | 1 | SHA-256 e remoção em caminho estendido |
| Outro título, interrupção, JSON truncado, banco/política divergente | — | checkpoint recusado |

A matriz acima valida o seletor de produtos isolado. No driver completo, alterar
nomes/inventários de includes pode exigir reconstrução conservadora: um arquivo
novo pode mudar a resolução de includes. Alterações de conteúdo sem mudar o
inventário continuam usando as dependências reais dos consumidores. Mudanças de
política, ferramentas/SDK e interrupções também exigem rebuild seguro.

O teste existente de recibos passou: substituição com mesmo tamanho/timestamp,
SHA-256, inventários, identidade AOT, DLLs, criação/substituição atômica e falha
de publicação com recibo bloqueado. O sandbox inicialmente recusou o rename no
diretório temporário desse teste; a repetição autorizada fora do sandbox passou.

O teste de toolchain passou com repetição determinística, caminhos com espaços,
novos includes sem extensão, extensão arbitrária e diretório local do fonte,
substituição de biblioteca com timestamp preservado e variável de ambiente usada
por CMake, sem persistir valores brutos. Os testes de travas e AOT genérico também
passaram: exclusão por título/diretório/sessão e invalidação por build direto.
O primeiro lançamento do teste AOT sem devshell falhou por CMake ausente do PATH;
a execução correta pelo devshell passou. Não se executou suíte completa.

O manifesto nativo também está declarado como `LINK_DEPENDS` nos alvos SDK/host,
além de `/MANIFESTINPUT:`. A suíte confirma que sua alteração exige link sem
recompilar fontes.

## Medições locais e limites

Evidências locais: `.local/evidence/build-incremental/`. O harness preserva bytes
e timestamp do fonte, limita cada build a 20 minutos, registra stdout/stderr e
copia logs. Logs antigos não contam como compilações da tentativa atual.

Condições: Windows x86-64 no mesmo host, presets existentes RelWithDebInfo,
`CMAKE_BUILD_PARALLEL_LEVEL=8`, LLVM 22.1.8
(`ca7933e47d3a3451d81e72ac174dcb5aa28b59d1`), CMake 3.31.6, Ninja 1.13.1
no AOT genérico de Charlie e Ninja 1.12.1 no preset Motocross. Cada par usa o
mesmo Ninja e diretório. SDK `f5337cdc947ff6d4c4196737e2c807a48f2a1fc2`
com os patches pinados até 0092; não há mudança de compilador ou flags para obter
os resultados. O driver anterior foi conservado em uma cópia local, alterando
somente raiz explícita e sufixos dos logs para não colidir com handles anteriores.

Mudança pequena: acrescentar um comentário ao fonte host (`src/main.cpp` do
Motocross; `main.cpp` do host genérico Charlie), restaurar imediatamente seu
`LastWriteTimeUtc` anterior, construir, e restaurar bytes e timestamp em `finally`.
Originais de jogos/dashboard nunca são alterados. Uma preparação posterior
certifica novamente o fonte restaurado. Contar somente linhas `Building ... object`
dos logs da tentativa, separando `.obj` e `.pch`; tempo de parede inclui inventários,
hashes, configurações, gerador, compilação, link e publicação.

A primeira preparação sem mudanças do Motocross reutilizou seu recibo em
14,36 s, sem invocar compilação. Seu contador inicial copiou contagens de logs
antigos; **1.127 nesse JSON não é quantidade de objetos compilados nessa tentativa**.
O stdout contém somente a reutilização verificada.

As primeiras tentativas de medir uma pequena alteração do Motocross não são um
par A/B concluído. Houve handle de log retido e uma tentativa foi interrompida
explicitamente para reservar o título ao outro trabalho autorizado. A tentativa
`before-small-final` durou 367,01 s até a interrupção, compilou 695 objetos do SDK
e gastou 191 s na etapa da ferramenta, sem concluir codegen/build final. O fonte
foi restaurado byte a byte. Não usar esses tempos como melhoria percentual.

Havia preparações de outros chats no mesmo ambiente, incluindo dashboard e
Charlie. Tempos sob essa concorrência são diagnósticos de parede, sem atribuição
causal universal. Contagens de objetos e seleção de consumidores são evidência
independente da variação de carga. O baseline completo posterior de Motocross foi
executado separadamente das medições dos outros títulos.

### Baselines completos

| Título, pequena mudança com timestamp preservado | Tempo total | Compilações `.obj` | PCH | Objetos únicos |
|---|---:|---:|---:|---:|
| Charlie Murder, driver anterior | 661,48 s | 962 | 42 | 958 |
| Motocross Madness, driver anterior | 670,66 s | 1.115 | 12 | 1.111 |

Charlie: ferramenta 241 s, codegen 18 s, configure/reconfigure 13 s cada e build
final 224 s; 1.004 ações de compilação ao incluir PCH. Motocross: ferramenta
132 s, codegen 58 s, configure/reconfigure 12 s cada, build final 300 s; 1.127
ações incluindo PCH. Quatro objetos aparecem duas vezes em cada baseline. O tempo
total inclui a coleta/publicação de recibos, além das etapas listadas.

A auditoria isolada do grafo Charlie encontrou 1.072 produtos, 958 objetos únicos
e 18 caminhos de produto acima de MAX_PATH; levou 53,32 s e selecionou exatamente
`main.cpp.obj` após um comentário com mesmo timestamp. Isso valida seleção, não
substitui a medição posterior de um build completo.
### Medição intermediária e correções de integração

Uma versão intermediária concluiu a migração segura de Charlie em 625,93 s, com 962 compilações de objetos
e 42 de PCH. Esse caso reconstrói todos os intermediários, pois a política do
driver mudou; não representa o estado incremental normal. A repetição imediata
sem mudanças concluiu em 49,00 s, com zero objetos/PCH compilados e reutilização
do recibo verificado. O inventário do ambiente e as verificações SHA-256 continuam
necessários nesse caminho; zero compilação não significa custo de preparação zero.
A repetição intermediária de 49 s expôs custo de descoberta de includes. O caminho
final pode reutilizar somente as **pastas** de dependências quando o recibo anterior
autentica o SHA-256 de `toolchain.json` e o SHA-256 de `.ninja_deps` continua igual.
Os nomes nas pastas são enumerados de novo; conteúdos de inputs/produtos continuam
sendo lidos para SHA-256. Não se reutiliza hash de conteúdo por tamanho/mtime.
Cache de pastas adulterado foi testado e descartado; a descoberta real o recompõe.

O primeiro build incremental real revelou `generated/default/dll_targets.cmake`
ausente do inventário upstream: esse arquivo auxiliar é escrito depois do stamp.
Isso impedia omitir configure e provocava recompilação de metadados. A medição foi
interrompida, o fonte restaurado e o recibo passou a autenticar todas as listas
`.cmake` nos roots gerados, inclusive auxiliares não incluídas pelo projeto atual.
Foi adicionado um caso focado que reproduz essa ausência. Uma auditoria posterior
confirmou os 203 inputs de configuração de Charlie e os 142 de Motocross
cobertos pelo novo recibo.
As tentativas interrompidas não entram na comparação final de tempos.

## Fechamento da auditoria sobre a main

Referência inicial: `6be0c60`, que contém `639e101`, `47d4cd0`, `7151666` e as
correções posteriores `65fd8d5`/`6be0c60`. As medições anteriores acima são
históricas e não substituem a validação desta revisão.

O fechamento reproduziu uma falha adicional: remover uma lista CMake auxiliar
não incluída no grafo podia deixar a comparação parcial satisfeita. O novo teste
falhou com `removed auxiliary CMake list ignored`. A correção `ead9ea3` armazena
um inventário separado de configuração no recibo e exige igualdade de quantidade,
caminhos e hashes, tanto para omitir configure quanto para reutilizar o produto
final. A publicação reconfere esse inventário após coletar o checkpoint. Recibos
anteriores sem esse inventário não autorizam omitir configure.

Regressões efetivamente executadas, com logs em
`.local/evidence/audit-closure-2026-10-18/`:

| Teste | Resultado | Tempo |
|---|---|---:|
| `incremental_test.ps1`, incluindo a proteção final de RSP | aprovado | 75,66 s |
| `toolchain_snapshot_test.ps1`, incluindo o negativo de RSP/ambiente | aprovado | 48,04 s |
| `x360n.preparation-receipt-publication` | aprovado | 2,37 s |
| `x360n.build-leases` | aprovado | 1,92 s |
| `x360n.generic-aot-build-lease` | aprovado | 5,78 s |
| `x360n.native-product-cleaning` | aprovado | 2,81 s |

As primeiras execuções no sandbox ficaram presas após emitir resultados parciais.
Somente suas árvores identificadas foram encerradas; não contam como aprovação.
As repetições acima rodaram fora do sandbox, com limite de 300 s nos testes
incremental/toolchain e os limites CTest existentes nos demais. Duas seleções
CTest iniciais encontraram zero casos na configuração antiga e também não contam
como teste aprovado; os scripts correspondentes foram executados diretamente.

### Comparação sequencial adicional

Driver `ead9ea3`, Charlie genérico, oito jobs, mesmas ferramentas/flags e diretório
das medições anteriores. Apenas o comentário no fonte host foi alterado, com
timestamp preservado; bytes e timestamp foram restaurados pelo harness.

| Execução | Parede | Compilações `.obj` | PCH | Resultado |
|---|---:|---:|---:|---|
| `audit-full` | 592,41 s | 962 (958 únicos) | 42 | rebuild completo certificado |
| `audit-incremental-restore` | 626,03 s | 962 (958 únicos) | 42 | **sem ganho**: snapshot da toolchain divergente exigiu rebuild seguro |
| `audit-stable-nochange` | 17,74 s | 0 | 0 | recibo reutilizado |
| `audit-stable-small` | 388,27 s | 19 | 0 | incremental certificado |
| `audit-stable-restored` | 382,89 s | 19 | 0 | fonte original novamente certificado |
| `audit-stable-final-nochange` | 18,03 s | 0 | 0 | recibo reutilizado |

O seletor removeu um objeto nos dois incrementais; o Ninja executou também 18
compilações AOT. Não apresentar a seleção de um consumidor como apenas uma
compilação efetiva. O build final levou 15 s em cada incremental; hashes,
inventários, produtor e publicação respondem pela maior parte da parede.
O completo levou 140 s na ferramenta, 14 s no codegen, 12 s em cada configure e
158 s no build final. As contagens incluem ferramenta e título, não logs antigos.

A divergência conservadora inicial não foi atribuída a uma causa sem evidência.
Na comparação seguinte, o snapshot autenticado permaneceu byte a byte idêntico
após nova coleta (11,35 s, `toolchain-roundtrip`); os dois no-ops e os dois
incrementais seguintes concluíram normalmente. O resultado sem ganho acima fica
preservado. São amostras locais com caches aquecidos, não promessa estatística
de aceleração nem medida de FPS.

### Flags externas não rastreadas

O fixture `external-response-red.json` demonstrou que trocar o conteúdo de um
`@flags.rsp` externo não mudava a assinatura: `compdb -x` expande apenas response
content declarado nas regras Ninja. O driver passa a recusar argumentos externos
ou aninhados que continuem como `@arquivo`, inclusive no ambiente de flags,
antes de certificar/reutilizar produtos. O erro é explícito; não se elimina a
verificação nem se declara essa forma externa suportada. Os response files
declarados pelo CMake/Ninja continuam expandidos e cobertos pelo teste positivo.
