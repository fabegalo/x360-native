# Preparação genérica de dependências AOT — 17/10/2026

Continuação do [relatório de descoberta](aot-discovery-2026-10-17.md).
ReXGlue permanece em `f5337cdc947ff6d4c4196737e2c807a48f2a1fc2`, LLVM 22.1.8,
agora com 88 patches ativos, até 0089 (0073 continua retirado). Sem mudança de renderer, stubs novos, push ou
publicação. A investigação de Castle está em
[relatório separado](castle-tail-boundary-2026-10-17.md).

## Reprodução preservada

O produto histórico do Charlie tinha somente o launcher. Seu recibo já estava
inválido após a reorganização dos caminhos; o supervisor de produção recusou a
execução corretamente. O recibo não foi editado para aparentar validade.

Para reproduzir somente a falha, EXE e DLLs históricos foram congelados em
`.local/build/diagnostics/aot-dependency-baseline/`, com hashes registrados.
O supervisor executou essa cópia diagnóstica, sem recibo de preparação novo,
com timeout de 25 s e store de usuário próprio. Resultado após 6.351 ms:

```text
Execute(82163C60): function not in function table
Indirect failure: LR=820407E0 CTR=82161600
STATUS_STACK_BUFFER_OVERRUN
```

Relatório: `.local/evidence/runs/20261018T002221Z-aot-dependency-baseline/report.json`.
`0x82163C60` continua sendo o entry point de `HostLoader.dll`, base `0x82120000`,
SHA-256 `f8e0a6ab7eba51c02333a84db88d146e591f25940be0967c970c8a926239b98a`.
Nenhum endereço foi acrescentado ao launcher.

## Implementação

A identificação/importação reconhece a assinatura XEX também em arquivos com
outras extensões e registra flags, base, tamanho e entry point de cada módulo.
Os inventários existentes permanecem utilizáveis: a preparação reinspeciona os
arquivos de seu inventário verificado, sem depender de listas antigas limitadas
à extensão `.xex`. Não há busca em outros títulos, versões ou diretórios irmãos.

A geração determinística cria `[[modules]]` para imagens executáveis DLL XEX2,
com `file_path` original e `guest_path` relativo completo. Recursos sem entry
point só permanecem exclusivamente como dados quando satisfazem a verificação
de metadata do carregador: DLL sem title/entry/imports/TLS, sem criptografia,
recursos limitados à imagem e páginas somente de dados cobrindo a imagem inteira.
Entrada zero sozinha não exclui uma DLL executável do AOT. XEX não identificado, executável
auxiliar sem identidade DLL e nomes de targets ambíguos são recusados antes de
publicar o projeto. Um DLL host comum não vira código PowerPC pela extensão.

O codegen e o registro de módulos continuam sendo os do SDK. Cada saída fica em
um diretório compacto independente no build; a integração CMake própria inclui
o `sources.cmake` correspondente, cria a biblioteca nativa e liga sua dependência
ao host. A lista de fontes é exportada para o coletor compartilhado das flags
PPC, incluindo o PCH. Não se alterou o template DLL do SDK, que pressupõe saídas
relativas ao projeto, nem se sintetizou `PPCFuncMappings` ou código guest.

O grafo Ninja alcança as DLLs a partir do host; o driver existente coleta suas
entradas/artefatos e os depfiles do codegen no recibo. A nova infraestrutura
`titles/common/generic/modules.cmake` também faz parte da identidade do build.
Os originais são dependências de configuração, capturadas antes do primeiro
codegen, mesmo sem depfile anterior. A seleção de DLLs nativas exclui os nós em
`.local/inputs/`: um XEX original chamado `HostLoader.dll` continua sendo input,
não um produto host a copiar ou certificar como DLL nativa.
O supervisor exige que **cada** módulo declarado esteja entre os inputs
certificados; omiti-lo de um recibo parcial não certifica a preparação.

O pacote local de Charlie contém **31 dependências XEX2 executáveis**, incluindo
HostLoader, os componentes XNA/Runtime em `Runtime/v4.0/` e `game.exe.xex`.
Todos são declarados pelos seus próprios arquivos. A preparação não renomeia,
realoca ou modifica as imagens. Sobreposições e falhas de análise continuam
sendo erros do pipeline, sem `--force`, fallback ou recibo parcial de sucesso.

## Análise opcional por identidade de módulo

A primeira análise integral parou em `MXF.Avatar.dll.xex`. A coleta isolada dos
31 módulos mostrou 30 erros de entrada em 29 módulos; HostLoader e RuntimeHost
não tinham erros estáticos. `titles/common/module_analysis.toml` acrescenta
somente as entradas originais conferidas, selecionadas pelo SHA-256 completo do
módulo e incorporadas ao manifesto. O catálogo de análise integra o recibo;
perfil malformado é erro. Outro hash não herda os endereços. Não há heurística
nova no scanner, patch de bytes, nome fictício ou implementação substituta.

Em 28 imagens StubBin, o único alvo pendente é o sufixo de cópia byte a byte.
As nove palavras foram desmontadas e comparadas em **cada** imagem:
`70A00003 7C0903A6 4DE20020 88040000 38840001 98060000 38C60001 4320FFF0 4E800020`.
O branch original para esse sufixo é registrado em cada grafo. Isso fundamenta
uma entrada compartilhada que usa o contexto PPC existente, sem mudar o corpo.

NetCFUserMode recebe duas entradas: `0x82BD4A00`, branch original de uma palavra
para `0x82BD76A0`, seguido de padding; e `0x82BE6DC8`, corpo original até o `blr`
em `0x82BE6E48`, antes da próxima `.pdata` em `0x82BE6E50`. Seus chamadores são
`b` em `0x82BE37E0` e `0x82BE35DC`, respectivamente. Desmontagens preservadas.

Os 29 retestes de análise passaram. Comparação dos grafos: 32 novas entradas
(30 configuradas e duas descobertas depois da resolução), 62 corpos antes
pendentes/ausentes passaram a ser emitidos, tabelas idênticas e nenhum fingerprint
alterado entre os corpos que continuaram emitidos. Uma entrada especulativa do
NetCF, `0x82BE6DE4`/104 bytes, foi absorvida pelo corpo correto em
`0x82BE6DC8`/132 bytes; os 104 bytes permanecem integralmente nesse intervalo.
As duas entradas adicionais (`0x82BE35E0`, erro original `0x80004005`, e
`0x82BE37E4`, `blr`) também foram conferidas. O erro retornado pertence ao
binário original; não foi criado um stub host. Grafos/comparação em
`module-analysis/`, `module-analysis-seeded/`, `module-analysis-comparison.json`.

## Bloqueio posterior do RuntimeHost

O codegen de produção dos 32 binários passou: 540 arquivos emitidos, registro
de módulos gerado, sem `--force`. A auditoria de 254 comandos de compilação PPC
encontrou zero comandos sem `-fno-strict-aliasing`/`-ffp-model=strict`.

O compilador C++ revelou outra limitação: o switch em `bctr 0x82471774`, tabela
byte-indexada `0x822742B8`, foi analisado com 512 slots. A função original
`0x82471730` possui `.pdata [0x82471730,0x82471880)`, mas a tabela especulativa
estendeu sua descoberta para a função seguinte. O emissor gerou, entre outros,
`goto loc_82471E48` sem label definido, e o build recusou o produto.

O limite real dos índices não foi demonstrado. A presença de outra tabela ou
de uma função vizinha não autoriza inferir 16 slots. Esse switch não recebeu
uma lista arbitrária, `--force`, stubs nem remoção de código. O RuntimeHost
continua sem build aceito, e a preparação completa do Charlie continua
incompleta, sem recibo novo de sucesso. Zero erros de análise não certifica C++
compilável, muito menos execução XNA.

Com o SDK final e `REX_HOST_FUNCTION_TABLE=1`, o reteste focado do objeto
`RuntimeHost/...recomp.62.cpp.obj` reproduziu os seis erros de labels ausentes
acima, exit 1 em 13 s (`runtimehost-final-mode-object.log`, prazo 150 s).
Assim, o bloqueio também está confirmado na configuração final, sem repetir
a compilação integral de todas as dependências ou publicar um recibo parcial.

## Despacho sem ocupar a imagem seguinte

O launcher ocupa `[0x82000000,0x82120000)` e HostLoader começa em `0x82120000`;
HostLoader ocupa 0x110000 bytes e RuntimeHost começa em `0x82230000`. Portanto,
as tabelas auxiliares padrão em `IMAGE_BASE + IMAGE_SIZE` invadiriam imagens
originais. Não se alterou base, tamanho ou endereço de função para acomodá-las.

Patch 0089 estende ao entrypoint o modo de despacho no host já existente para
DLLs. A sobrecarga anterior de `Runtime::Setup` continua exportada e delega ao
modo padrão; a nova aceita o modo explicitamente. `PPCImageInfo` e RuntimeConfig
mantêm seus layouts. Um hook opt-in no ReXApp seleciona a sobrecarga; os perfis
existentes mantêm false. A integração genérica com dependências compila host,
objetos PPC e DLLs com `REX_HOST_FUNCTION_TABLE=1`, em acordo com o export de
capacidade consultado pelo carregador. Projetos genéricos sem DLL continuam no
caminho anterior. O pin do SDK, os patches anteriores e seus testes permanecem.

O hook acrescenta um slot virtual ao ReXApp: consumidores nativos desse header
devem ser recompilados e distribuídos com suas DLLs correspondentes. Não se
certificou mistura de um EXE nativo antigo com o runtime novo. Os recibos incluem
headers/lock e artefatos para recusar essa combinação; as preparações focadas
abaixo refazem seus produtos. Isso não altera o ABI nem os bytes dos XEX guest.

Red final: 15/16 asserções, falha no registro de uma imagem adjacente usando o
único caminho de entrypoint disponível antes do patch. Green: 30 casos / 1.075
asserções de entrypoint, tabelas, módulos, perfis, retorno e C8 aprovados.
Replay em LF reproduziu os quatro hashes finais do patch; a primeira cópia de
replay sob `core.autocrlf` gerou CRLF e ficou preservada como tentativa sem
igualdade de hash. O lock registra hashes de base e finais; 88 patches checados.

O modo evita tabelas residentes no espaço guest e usa o resolvedor compartilhado
existente nas chamadas indiretas. Seu custo em jogos multimódulo não foi medido;
não é uma otimização de FPS. A correção é necessária para manter os endereços
originais. Não há scanner, JIT ou compilação em tempo de execução.

## HostLoader compilado e registro nativo

O target explícito da DLL HostLoader compilou com o modo coerente de despacho
no host. O main genérico também passou por compilação sintática com seu comando
real do Clang/devshell, sem tentar contornar o erro do RuntimeHost.

`x360n_aot_module_registration_probe` é opt-in, fora de ALL/CTest. Carrega a DLL
real, chama seus exports de layout e registro através de `IModuleRegistrar`
(apenas endereços e ponteiros; sem trocar objetos de runtime/containers CRT),
confere o XEX e verifica que o ponteiro da entrada pertence à DLL carregada.
Não executa funções guest nem publica recibos de título.

Run final limitado a 15 s, concluído em 305 ms:

```text
original_sha256=f8e0a6ab7eba51c02333a84db88d146e591f25940be0967c970c8a926239b98a
native_sha256=90e12d61e746fca1f753681a06c4c70ff1a2fe068ad6f4f675193a5c95f03793
mappings=1461
image_base=82120000 image_size=110000 entry=82163c60
entry_owned_by_native_module=true
host_function_table=true
```

Zero erros, critical, stubs ou mensagens unimplemented no processo do probe.
Relatório `.local/evidence/runs/20261018T012647Z-hostloader-0089-registration/`.
As dependências exatas foram congeladas em
`.local/build/diagnostics/hostloader-0089/`, com hashes em
`hostloader-0089-binaries.json`. O primeiro probe, antes do modo host, também
foi preservado (`20261018T010921Z-hostloader-native-registration`); não representa
o produto final. A presença do entry point nativo resolve a lacuna de
preparação/registro identificada no launcher antigo, mas não demonstra DllMain,
execução XNA ou lançamento completo pela My Games.

## Evidência e limites

Raiz local desta etapa: `.local/evidence/aot-dependencies-2026-10-17/`.
O projeto histórico está em `original-project/`; binários, recibos e logs
anteriores permanecem preservados. Os seguintes resultados se sobrepõem onde
há retestes; não somar suas contagens como uma suíte única:

| Verificação | Resultado |
|---|---|
| Importação/AOT genérico/My Games sintético, todos os cenários | 13 casos / 191 asserções aprovados |
| Geração AOT genérica após o modo de despacho host | 3 casos / 87 asserções aprovados |
| Recibos e isolamento de recursos de dashboard | 11 casos / 197 asserções aprovados |
| Sessão/recibos/isolamento, reteste ampliado no host | 24 casos / 33.242 asserções aprovados |
| Memória/despacho, propriedade de módulos, retorno e C8 | 17 casos / 755 asserções aprovados |
| Descoberta GapFill/0088 | 5 casos / 120 asserções aprovados |
| Imports, recursos XEX e propriedade de heap | 5 casos / 151 asserções aprovados |
| Identidades locais dos títulos de integração | 1 caso / 62 asserções aprovados |
| Publicação atômica dos recibos, PowerShell | Aprovada no host |
| Regeneração real do Charlie, inventário/catálogo preservados | 1 caso / 12 asserções aprovados |

Uma análise isolada adicional do `RuntimeHost.xex` local (SHA-256
`d623d2b533f340cb9a55e0a2bebd42fc9d056805d14bcc9b97669d0aa4d91e32`)
encontrou 19.634 funções, 1.699.092 palavras cobertas e zero erros estáticos;
entry point `0x82376AE0` presente. Grafo em `runtimehost-probe.tsv`. Isso não
executa o runtime XNA nem prova a cobertura das chamadas indiretas.

As primeiras execuções de fixtures sob sandbox falharam em rename/publicação
atômica. Os logs foram mantidos e os testes relevantes repetidos no host;
essas falhas de ambiente não foram contadas como aprovações. Os primeiros builds
dos testes também registram os includes TOML/Catch inicialmente ausentes.

A primeira configuração nova do Charlie foi interrompida após 175 s para
incorporar a exportação das fontes de DLL ao contrato PPC, antes de codegen ou
publicação de recibo. Seu log e a saída da interrupção estão preservados.

Compilação, entrada no runtime, execução por My Games e jogabilidade são fatos
distintos. Os testes de fixtures não demonstram gameplay de XNA, saves comerciais
ou ganho de desempenho. A descoberta roda durante a preparação; não foi
introduzida varredura de arquivos ou compilação em tempo de execução.

## Repetição focada e preservação

As preparações usam `scripts/build-title.ps1 -Title <id>` na raiz, que invoca
`scripts/devshell.cmd` em cada etapa. IDs desta etapa: `iruka`,
`motocross_madness`, `castle_crashers`, `dash_17559` e o Charlie genérico
`g0a4c6253b34297dc971555c160fb8affde66f554296bd83bae609a48d5f5b47`.
O probe de registro é um target explícito do build Debug:

```powershell
scripts/devshell.cmd cmake --build .local/build/win-amd64 --config Debug --target x360n_aot_module_registration_probe
```

Seus argumentos são XEX original, DLL nativa e entry point hexadecimal esperado.
Usar a DLL do target específico não publica recibo do host incompleto.
`tests/supervisor/preparation_receipt_test.ps1` exercita a classificação dos
artefatos; `scripts/setup/apply-sdk-patches.ps1 -Check` confere o SDK aplicado.
Os seletores Catch usados nesta etapa estão nos logs focados citados acima;
nenhuma suíte completa foi executada.

Antes de qualquer sessão de UI, a conferência integral dos **983 arquivos** do
inventário Charlie encontrou zero diferenças de SHA-256. Os cinco arquivos
monitorados de catálogo, perfil e biblioteca também permaneceram idênticos:
`charlie-inventory-final.json`, `preservation-before.json` e
`preservation-after-builds.json`. Recibos anteriores foram copiados para
`previous-*.preparation.json`; não foram reescritos para certificar binários
incompletos. Todos esses artefatos ficam sob a raiz local de evidência.

Commits locais de implementação: `b30baa7` (despacho opt-in para entrypoint) e
`26de096` (descoberta, preparação, análise por hash e registro de dependências).
Harness Debug com escopo por processo e lista de jogos: `112a3a8`.

## Builds de regressão

O produto desktop `x360-native` concluiu o build Debug pelo devshell com o
gerador atualizado (`desktop-product-final-build.log`); não foi lançado pela UI.
O supervisor usado nas validações também é Debug. Nenhum build de configuração
Release ou pacote/Release público foi criado nesta etapa.

Iruka concluiu a preparação com o SDK de 88 patches em 312 s. Motocross concluiu
em 961 s na tentativa com `CMAKE_BUILD_PARALLEL_LEVEL=4`, incluindo codegen,
compilação e certificação. Ambos publicaram recibos novos e retiraram seus
marcadores de preparação incompleta.

Castle concluiu com recibo novo em 341 s (oito jobs), incluindo codegen e build
do título; seus hints e switch manuais de produção não foram alterados.

A tentativa anterior do Motocross registrou vários `FAILED` nos objetos
`default.189`–`default.198`, sem mensagem de erro do Clang no log, e foi encerrada
pela supervisão. Não há diagnóstico suficiente para atribuir a falha a memória
ou disco. Log e saída do driver foram preservados como
`motocross-0089-interrupted-*`; não foram contados como aprovação. Todos esses
objetos foram gerados na tentativa posterior bem-sucedida.

Após Moto, a etapa inicial de Castle foi interrompida para mudar o limite dos
dois builds restantes de quatro para oito jobs. Identidade do driver e árvore
de processos próprios foram verificadas antes do encerramento. Registro em
`parallelism-adjustment.txt`; cada nova preparação mantém prazo de 1.800 s e
limpeza de seus filhos. Esses tempos incluem rebuilds e hashing de recibos,
com condições diferentes, e **não são comparação de performance**.

17559 concluiu em 582 s, incluindo os codegens separados de sign-in/Guide e o
build nativo de 219 s. Sua etapa principal de codegen levou 36,5 s: 12 arquivos
escritos, 416 inalterados. Os quatro recibos finais incluem o mesmo lock atual:

| Título | Preparação | Inputs certificados | Artefatos certificados |
|---|---:|---:|---:|
| Iruka | Aprovada | 2.963 | 4 |
| Motocross Madness | Aprovada | 3.620 | 6 |
| Castle Crashers | Aprovada | 2.970 | 5 |
| Dashboard 17559 | Aprovada | 3.450 | 13 |
| Charlie Murder | **Incompleta**, RuntimeHost não compila | Sem recibo novo | HostLoader isolado não certifica o host |

Hashes dos EXEs/recibos, commit SDK, lock e contagens em
`final-preparation-matrix.json`. Logs completos em `build-<id>.log` e nos
`x360n-build-*.log` dos respectivos diretórios de build.

## My Games original e smokes finais

O registro real da biblioteca pelo modelo de produção passou em **1 caso / 7
asserções** (`library-registration-final.log`, prazo 120 s). Revalidou a
preparação da 17559, Moto e os recursos associados, publicou o cabeçalho Ready
do Castle e conferiu a biblioteca. Uma invocação anterior escolheu o executável
de testes errado e não executou casos (`library-registration-wrong-target.log`);
isso não foi contado como aprovação.

Iruka executou por 15.239 ms no supervisor Debug, com store diagnóstico próprio,
e foi encerrado pelo timeout previsto: exit do supervisor 2, sem encerramento
normal do jogo. Zero stubs, unimplemented, erros ou critical nesse intervalo.
Run `20261018T021632Z-aot-deps-iruka-0089`. É um smoke de inicialização, sem
entrada, gameplay ou medição de FPS.

Sessão original: `.local/evidence/validation/aot-deps-0089-mygames-1/`.
Prazo global de 420 s; `--skip-boot-intro` evita repetir o aceite da animação.
Renderer global **Xenos**, sem alteração de configuração. O harness agora usa
Debug por padrão e restringe cada entrada/captura à lista explícita de jogos e
ao filho direto de seu supervisor. As **36 ações** passaram essa verificação,
todas na mesma janela `3737706`; nenhum processo de outro aplicativo foi usado.

Fluxo observado e capturado:

1. Home 17559 → Jogos → Meus Jogos → Castle Crashers → menu original do trial.
2. Guide original → Início do Xbox → confirmação Sim → Home 17559.
3. Meus Jogos → Motocross Madness → tutorial existente, sem avançar a condução.
4. Guide original → Início do Xbox → confirmação Sim → Home 17559.
5. Meus Jogos → pasta antiga do Charlie → recusa de preparação pelo supervisor.

Os dois jogos exportaram pedidos originais de retorno. O log confirmou os hashes
dos pedidos: Castle `825e9c8cb951d62ce954901488c5b783349c98cc5c03bd08ea0739df3af5d9ad`,
Moto `2a8406bcf9c223f9d7da732be617cc7cd46fbb587ef552f7fcacac2049b95dcd`.
Não houve comando de fechar janela, cleanup ou timeout para simular esses retornos.

| Run em `.local/evidence/runs/` | Duração | Exit | Stubs | Unimplemented | Erros / critical |
|---|---:|---:|---:|---:|---:|
| `20261018T021734Z-session-0-dash_17559` | 85.170 ms | 0 | 16.296 | 4.945 | 5.006 / 0 |
| `20261018T021911Z-session-1-castle_crashers` | 59.153 ms | 0 | 178 | 0 | 0 / 0 |
| `20261018T022019Z-session-2-dash_17559` | 56.880 ms | 0 | 11.389 | 3.269 | 3.312 / 0 |
| `20261018T022130Z-session-3-motocross_madness` | 84.655 ms | 0 | 175 | 0 | 1 / 0 |
| `20261018T022304Z-session-4-dash_17559` | 48.712 ms | 0 | 9.690 | 2.782 | 2.822 / 0 |

Nenhum dos cinco runs registrou chamada AOT ausente. Os contadores de stubs
pertencem aos serviços já existentes do SDK; não foram acrescentadas implementações
falsas. A dashboard continua registrando configurações de perfil e mensagens
XLIVEBASE não implementadas, além de `DASHUSER:` ausente. Moto registrou a
consulta `devkit:\senoverride\0x58410b0f\transport.ini` sem dispositivo, já
classificada no [relatório do título](titles/validation-motocross-madness.md):
é consulta do original em um ambiente retail, não motivo para inventar um mount.
Os relatórios também preservam os failed opens e variáveis kernel ausentes.
Os ciclos observados não são um aceite de todos os serviços XAM, gráficos,
licenciamento, campanha ou saves comerciais.

O My Games ainda enumera duas pastas antigas sem cabeçalho ativo, exibindo seus
nomes de hash. Esse limite da biblioteca foi preservado, sem excluir conteúdo
ou restaurar artificialmente registros incompletos. A tentativa do Charlie
produziu o pedido original para
`.../58410B83/000D0000/0a4c6253b34297dc971555c160fb8affde66f554/default.xex`,
flags `00000000`, e terminou com **`no verified AOT profile`**, exit global 1.
Nenhum processo novo do Charlie foi criado. Isso valida a recusa pelo gate;
**não valida a execução nova do Charlie pelo My Games**. A reprodução de
`0x82163C60` e o probe nativo do HostLoader são as evidências separadas acima.

Após a sessão: os **983 originais** do Charlie continuam idênticos; os cinco
arquivos monitorados de catálogo/perfil/cache e os **seis cabeçalhos anteriores**
(ativos e arquivados) mantêm seus SHA-256. Configuração gráfica também idêntica.
O único cabeçalho publicado foi o do Castle com preparação válida. Cópias dos
cinco arquivos anteriores permanecem em `preserved-user-before-session/`.
Evidência: `charlie-inventory-after-session.json`, `preservation-after-session.json`,
`preservation-session-diff.json`, `library-headers-before-session.json`,
`graphics-before-session.json` e `mygames-final-summary.json`. Todos os filhos
de título da sessão terminaram; nenhum build ou jogo desta etapa ficou rodando.
