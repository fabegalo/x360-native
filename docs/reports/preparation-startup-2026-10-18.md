# Preparação e lançamento — 18/10/2026

Base: `c84a4b4`, `main` local. Escopo: preparação e validação de lançamento;
abertura, listagem do catálogo, renderer, PPC, XNA e NetCF não são alterados.
Nenhum jogo é executado pelos benchmarks deste relatório. Não há push/Release.

## Causas e implementação

- O cálculo de checkpoints percorria o grafo e centenas de milhares de registros
  de headers pelo pipeline PowerShell. O helper compilado mantém comandos
  expandidos, arestas reais, headers consumidos, bibliotecas implícitas, recursos,
  PCH, produtos irmãos e SHA-256. Sua fonte integra política, snapshot do driver
  e recibo. Intermediários sem procedência continuam exigindo rebuild seguro.
  A transição do algoritmo anterior de `c84a4b4` exige hashes exatos dos três
  scripts anteriores no checkpoint e recibo, com toda a política restante
  idêntica. Não aceita outro compilador, SDK, patches, ambiente, raiz ou
  predecessor desconhecido. Cada produto ainda recebe SHA-256 novo e assinatura
  recalculada antes de ser consumido; não existe migração por versão nominal.
  A segunda otimização reconhece também os hashes exatos do checkout medido de
  `f11faaa` e do estado diagnóstico `7beab02`, sem aceitar modificações em
  outros componentes da política. Esses predecessores são reconhecidos por hashes
  exatos; não há aceitação genérica de checkpoints antigos.
- A autenticação no .NET Framework usava blocos de 4 KiB. A leitura em blocos de
  1 MiB reduz chamadas de leitura/hash, mantendo SHA-256 integral a cada chamada.
  Coleta e conferência de arrays de recibos passam ao helper compilado. Não há
  cache de aprovação nessas rotinas: cada arquivo é relido em cada chamada.
  A identidade da fonte compilada fica vinculada ao host PowerShell; mudar a
  fonte no mesmo host exige reiniciar o host, em vez de usar código obsoleto.
- Alterar somente o host nativo também invocava o gerador. O caminho novo pode
  reutilizar AOT quando produtor, checkpoint, manifestos, depfiles, originais,
  análises recursivas, stamps e todos os outputs estão autenticados por conteúdo.
- O desktop autenticava o conjunto e soltava os handles antes de o supervisor
  autenticar novamente. O supervisor agora executa a mesma conferência de
  conteúdo, recibos, dependências e My Games em uma única `DigestBatch`, seguida
  das verificações de sessão e das identidades do processo. Não há preparação
  implícita no lançamento. Falhas retornam o diagnóstico ao desktop.
- Canonicalização repetida de ancestrais e reabertura dos mesmos arquivos
  dominavam parte da validação. Caminhos ordinários podem ser reutilizados
  somente com folhas e ancestrais protegidos contra escrita/rename. Links,
  identidades indisponíveis e plataformas sem exclusão equivalente preservam
  resolução/leitura conservadoras. Handles são soltos antes do runtime; cada
  transição e cada lançamento começa uma transação nova.
- As tabelas da transação usam consultas por hash, sem varreduras ordenadas de
  caminhos longos. Igualdade continua sensível a caixa; aliases são autenticados
  por identidade de arquivo. O profiling também encontrou cerca de quatro segundos
  na releitura e validação dos metadados de catálogo. Somente no escopo
  de lançamento, esses mesmos bytes passam pelo provedor SHA-256 do Windows.
  Vetores e fronteiras de padding/blocos são comparados com a implementação
  portátil; indisponibilidade/erro do provedor falha de forma explícita. O fluxo
  padrão de abertura conserva seu algoritmo e suas verificações anteriores.
  Consultas de nomes exatos já protegidos evitam decomposição/normalização
  repetida; nomes novos, dot components, links e aliases não vistos passam pelo
  caminho completo. A detecção de duplicatas do inventário usa hash somente no
  escopo novo; o leitor padrão mantém seu conjunto ordenado anterior.
- O SDK regrava stamps de manifestos sem mudar os bytes. O driver conserva seus
  metadados anteriores somente após SHA-256 idêntico, antes de entrar no Ninja e
  novamente depois dele. Manifesto alterado/ausente nunca recebe essa restauração.
  Isso evita que uma escrita idempotente dispare CMake, altere `rex/version.h`,
  recompile o produtor e force análise PPC/AOT desnecessariamente. Não se restaura
  metadata de produtos nem se decide autenticidade por timestamps.
- O ensaio incremental real encontrou outra causa: objetos AOT com caminhos
  acima de `MAX_PATH` ficavam com `#deps 0, mtime 0 (STALE)` no Ninja 1.13.1.
  O seletor invalidava um host, mas o Ninja recompilava outros 18 objetos e
  relinkava oito DLLs. O template genérico agora usa nomes privados compactos,
  determinísticos e com rejeição explícita de colisões. Nomes públicos de build,
  DLL/PDB, macro de exportação, fontes e flags PPC/PCH permanecem iguais.
  Não há troca de Ninja, SDK, alteração de registro do Windows ou proxy de fonte.
- A geração de projeto AOT criava `project-generation` mesmo sem alterar arquivo
  algum, fazendo uma preparação idêntica parecer interrompida. Agora só cria o
  marcador antes da primeira escrita que muda bytes. Um marcador de falha anterior
  continua presente. Originais e identidades XEX usam uma transação protegida
  durante a geração; os handles são liberados antes de escrever os arquivos do
  projeto. Cancelamento externo é herdado pelas validações aninhadas.

Não há cache persistente de aprovação, aprovação por mtime/tamanho, cache global
de módulos originais, mistura de dashboards ou mudança de flags/semântica PPC.
Não se alteram serviços XAM, perfis, saves, SDK pinado ou seus patches.

## Metodologia e evidência

Evidência local: `.local/evidence/preparation-startup-2026-10-18/`.
Windows x64, LLVM 22.1.8, CMake 3.31.6, mesmos presets de títulos e oito jobs.
Driver e benchmarks no Windows PowerShell 5.1, host usado oficialmente pelo
preparador. Os testes focados de recibos/helper também passaram no PowerShell 7;
isso não certifica o pipeline completo de títulos nesse outro host.
Frontend/benchmarks Debug com `-march=x86-64-v2`; os títulos preservam seus
presets existentes. Cache do SO não foi purgado. Repetições são quentes ou com
cache não controlado; rebuild completo de produtos não significa disco frio.

`x360n_launch_benchmark` termina antes de criar processo/janela de título.
Compara preflight + validação do supervisor, registrando parede, CPU, leituras
lógicas, operações e pico de working set. Não mede gameplay, carregamento real
de DLLs, primeira janela guest nem primeiro frame.
O baseline usa os verificadores originais em um executável com o mesmo código
de benchmark e flags. Seu relógio começa após carregar o catálogo, pois o desktop
anterior já tinha esse snapshot. O candidato inclui o custo adicional da leitura
fresca do catálogo no supervisor. A abertura/listagem existente não foi alterada.
As passagens preliminares que cobravam catálogo nos dois caminhos não são usadas
para calcular o ganho final de lançamento.
Nos conjuntos de jogos, também não simula a navegação My Games, argumentos de
avatar ou o handoff real da dashboard; esses trechos dependem do ensaio manual.

`x360n_preparation_benchmark` chama somente o driver oficial, com deadline de
20 minutos e Job Object que inclui seus descendentes. Registra CPU agregada,
I/O lógico e pico de commit do job; isso não é pico de RAM física nem I/O físico.
Objetos/PCH devem ser contados apenas nos logs únicos da tentativa correspondente.
O campo `launched` nesse JSON refere-se ao processo PowerShell do preparador;
não representa execução de jogo/dashboard.
Os tempos do driver não incluem provisionamento de ferramentas, geração de
projeto e registro My Games no botão Preparar. A geração idempotente de Charlie
foi verificada separadamente: **4.373,1 ms**, recibo continuou válido e nenhum
marcador foi criado (14 asserções). Não se somam amostras de fases distintas para
alegar uma medição integral da interface.

Comparações do código anterior preservam cópias locais verificáveis. Quando
necessário, fontes certificadas são restauradas temporariamente sob exclusão,
com reposição dos candidatos e timestamps em `finally`. Recibos não são editados
para aprovar produtos. A tentativa chamada `before-noop-dash_17559` entrou em
rebuild seguro: deve ser classificada como reconstrução completa, não como no-op.

## Preparação completa

Reconstrução nativa completa da 17559 no mesmo diretório e preset, incluindo
ferramenta/SDK, gerador e módulos. O candidato recebe marcador de preparação
incompleta sob a trava oficial para exigir recuperação conservadora. Somente
produtos derivados são invalidados pelo driver; os conteúdos originais não mudam.

| Métrica | Anterior | Atual | Redução |
|---|---:|---:|---:|
| Parede | 703,119 s | 552,647 s | 21,40% |
| CPU agregada da árvore | 2.964,609 s | 2.698,641 s | 8,97% |
| Leituras lógicas | 14.178.459.797 B | 13.141.010.993 B | 7,32% |
| Operações lógicas de leitura | 3.909.787 | 924.145 | 76,36% |
| Escritas lógicas | 1.549.676.787 B | 1.589.518.536 B | **aumento de 2,57%** |
| Pico de commit do job | 5.111.480.320 B | 4.965.871.616 B | 2,85% |
| Ações de compilação de objetos | 1.019 | 1.019 | 0% |
| PCH | 18 | 18 | 0% |

São 1.015 caminhos únicos de objeto nas duas reconstruções; quatro objetos
aparecem duas vezes. Ferramenta: 135 → 138 s; codegen principal: 39 → 38 s;
configure/reconfigure: 11 → 8 s cada; compilação final: 213 → 197 s.
Não houve troca de flags ou mudança do compilador/linker: a pequena variação das
fases nativas não prova aceleração do Clang. O ganho principal está na análise,
autenticação e coleta de dependências/produtos. O cache do SO não é controlado.
O candidato medido corresponde a `98a915d`, em `final-preparation-dash_17559`.
A primeira otimização (`f11faaa`) chegou a 537,739 s; foi mantida como resultado
intermediário, sem substituir o tempo da versão final pelo melhor da série.

## No-op final

Cada chamada abaixo autentica o recibo completo e retorna antes de CMake,
Ninja, codegen e compilação. Zero objetos/PCH/DLLs reconstruídos nos quatro casos.

| Título | Antes | Final | Redução de parede | Redução de CPU | Redução de operações de leitura |
|---|---:|---:|---:|---:|---:|
| 17559 | 20,554 s | 14,019 s | 31,79% | 40,24% | 91,82% |
| Charlie Murder | 18,544 s | 12,431 s | 32,96% | 43,98% | 91,54% |
| Motocross Madness | 21,471 s | 14,667 s | 31,69% | 39,24% | 92,55% |
| Castle Crashers | 17,897 s | 12,154 s | 32,09% | 43,75% | 90,54% |

O volume lógico permanece aproximadamente 1,22–1,70 GB por chamada: os bytes
continuam sendo lidos, sem cache persistente de SHA. O pico de commit do job
foi 365,05 → 332,19 MiB na 17559; 338,41 → 326,75 MiB no Charlie; **369,21 →
371,55 MiB no Moto (+0,63%)**; **292,41 → 329,84 MiB no Castle (+12,80%)**.
Isso inclui PowerShell/compilador do helper e não representa RAM física do jogo.
As amostras finais estão em `verified-noop-*`; são novas instâncias de PowerShell,
com cache do SO quente/não controlado, e não simulações com hashes já aprovados.
A rodada anterior `final-noop-*` apresentou 32,16–34,21% de redução. A tabela
usa a repetição após os últimos commits, sem escolher a menor amostra de cada título.

## Incremental real após a correção de paths

Um comentário foi acrescentado ao host com timestamp preservado, seguido de
restauração exata e nova preparação oficial. Apenas o host mudou; originais de
jogos, perfis e saves não foram modificados. Evidência: `fixed-incremental-*` e
`fixed-restored-*`, com logs de estágio copiados e `compile-actions.json`.

| Operação | Parede | Objetos C/C++ | PCH | Configure | Codegen explícito |
|---|---:|---:|---:|---:|---:|
| Charlie, comentário | 136,055 s | 1 | 0 | 0 | 14 s |
| Charlie, fonte restaurado | 123,483 s | 1 | 0 | 0 | 0 |
| Motocross, comentário | 166,835 s | 1 | 0 | 0 | 0 |
| Motocross, fonte restaurado | 167,673 s | 1 | 0 | 0 | 0 |

| Operação | CPU agregada | Leitura lógica (GB decimais) | Operações de leitura | Pico de commit do job |
|---|---:|---:|---:|---:|
| Charlie, comentário | 148,250 s | 20,396 | 545.933 | 1.055,53 MiB |
| Charlie, restauração | 136,047 s | 20,357 | 536.227 | 974,31 MiB |
| Motocross, comentário | 220,672 s | 19,762 | 814.812 | 1.046,72 MiB |
| Motocross, restauração | 223,125 s | 19,762 | 814.786 | 1.052,04 MiB |

Todas as **34 DLLs de Charlie e 5 de Motocross** mantiveram SHA-256 idêntico
após a alteração. Não houve link de DLL nesses quatro builds; somente o host
foi recompilado/relinkado. A restauração confirmou bytes e timestamps originais
em `source-restoration.json`; os recibos foram publicados pelo driver oficial.

A primeira passagem de Charlie ainda invocou codegen após a migração. O motivo
exato do gate não foi registrado nessa tentativa; não se atribui essa invocação
a uma mudança específica do produtor sem evidência. Ela não é apresentada como
incremental estável sem geração. Na restauração seguinte e nos dois casos Moto,
análise e outputs AOT foram reutilizados integralmente.

A correção de paths eliminou as 18 compilações extras e oito links de DLL de
Charlie, mas **não demonstrou ganho de parede no incremental estável**:
restauração 123,363 → 123,483 s (+0,10%, sem ganho); CPU 161,891 → 136,047 s.
Os inventários e a autenticação dos produtos entre fases continuam dominando o
driver. Não há baseline pareado do driver completo `c84a4b4` para essa pequena
alteração, portanto não se fabrica um percentual contra ele. O ganho de 84,25%
do núcleo de fingerprints é uma medição isolada, descrita separadamente.

## Lançamento: validação anterior ao processo

Revisão `d48fdfe`; dados `verified-final-*` e `verified-final-summary.json`.
Uma passagem de aquecimento excluída e ordem ABBA por título, duas amostras
válidas por implementação, processos novos, sem builds/sessões concorrentes.
O candidato inclui sua releitura de catálogo de 3,82–3,94 s; o baseline começa
após o catálogo que o desktop já possuía. Não há codegen ou compilação nesse
ensaio. Todos os quatro títulos foram antes certificados pelo driver oficial.

| Título | Antes | Atual | Redução de parede | Redução de CPU | Redução de operações de leitura |
|---|---:|---:|---:|---:|---:|
| 17559 | 28,789 s | 21,872 s | 24,03% | 24,47% | 21,60% |
| Charlie Murder | 10,628 s | 9,299 s | 12,51% | 12,49% | 35,68% |
| Motocross Madness | 12,245 s | 10,745 s | 12,25% | 12,16% | 37,94% |
| Castle Crashers | 8,680 s | 8,151 s | 6,09% | 6,59% | 39,85% |

| Título | Streams SHA antes → atual | Leituras lógicas antes → atual (B) | Pico de working set antes → atual |
|---|---:|---:|---:|
| 17559 | 15.910 → 11.036 | 15.735.855.156 → 13.967.154.415 | 36,89 → 58,79 MiB |
| Charlie | 10.107 → 6.021 | 3.481.718.628 → 2.120.311.142 | 34,92 → 47,04 MiB |
| Motocross | 8.464 → 4.287 | 6.415.232.166 → 4.780.604.148 | 33,13 → 43,18 MiB |
| Castle | 7.760 → 4.234 | 2.472.340.178 → 1.312.158.818 | 33,02 → 42,71 MiB |

Os bytes efetivamente submetidos a SHA caíram 11,26% / 39,23% / 25,54% / 47,10%,
respectivamente. As identidades obrigatórias continuam verificadas; eliminam-se
streams repetidos enquanto handles protegem o mesmo conteúdo. Leituras lógicas
totais incluem metadados e parsing, além dos bytes de SHA.

O pico de memória do processo de benchmark **aumentou** entre 9,68 e 21,90 MiB
pela retenção de identidades/handles até terminar a transação. Não é memória do
runtime nem pico agregado desktop + supervisor + jogo. Não se alega redução de
memória ou carregamento mais rápido das DLLs do título a partir desses dados.

Faixas de parede antes → atual: 17559 28,734–28,844 → 21,791–21,952 s;
Charlie 10,628–10,628 → 9,235–9,362 s; Moto 12,236–12,254 → 10,723–10,767 s;
Castle 8,661–8,698 → 8,128–8,173 s. São amostras quentes locais, sem intervalo
de confiança ou alegação de resultado universal. A redução de Castle é menor.

O ganho acima é uma melhoria independente do preparador. **Não mede clique →
processo real, carregamento efetivo de DLLs, janela guest ou primeiro frame**.
Esses intervalos permanecem reservados ao teste manual descrito abaixo.

## Iterações e falhas preservadas

O primeiro ensaio de fonte host em Charlie terminou o driver em 124,304 s, mas
foi **recusado** pelo harness porque DLLs não relacionadas mudaram. Houve 19
objetos, zero PCH e oito links de DLL; a restauração do fonte repetiu as 19
compilações em 123,363 s. O seletor havia apontado somente um objeto, por isso
contar apenas sua mensagem teria produzido uma conclusão falsa. Bytes/timestamp
do fonte foram restaurados e certificados pelo driver; o ensaio não avançou para
Motocross antes de corrigir a causa.

Um objeto de GamerServices media 265 caracteres de caminho completo; seu depfile
também excedia o limite. O fixture reproduziu o rebuild infinito. Encurtar apenas
o depfile passou a registrar os headers, mas deixou `mtime 0 (STALE)` e não resolveu
o problema; executar Ninja com CWD estendido tampouco funcionou com Clang. Essas
tentativas ocorreram somente em projeto sintético. O uso de APIs ANSI está no
[código do Ninja 1.13.1](https://github.com/ninja-build/ninja/blob/v1.13.1/src/disk_interface.cc)
e na [leitura de depfiles](https://github.com/ninja-build/ninja/blob/v1.13.1/src/util.cc).
Targets privados curtos resolveram ambos os limites sem alterar fontes gerados.
A recuperação oficial de Charlie levou 398,792 s, com reconstrução conservadora;
é migração de layout, não tempo de incremental/no-op nem comparação de build frio.
As **31 DLLs AOT reais** mantiveram exatamente nomes e ordinais de exportação.

O fixture novo de paths cobre build pelo nome público, DLL original, define de
exportação, determinismo, dependências completas, PCH/FP, no-op sem link e
invalidação por header. Sua primeira execução rápida encontrou logs retidos pelo
helper assíncrono de telemetria do VsDevCmd. O fixture usa LLD pinado e desliga
somente essa telemetria no seu processo; a execução corrigida passou. Nenhuma
variável global, instalação ou script de devshell do produto foi alterado.
Uma checagem local adicional foi bloqueada por um build do frontend iniciado em
paralelo; não conta como aprovação. A repetição seriada passou sem mudar o recibo.

A primeira comparação justa de lançamento incluiu a leitura nova do catálogo
somente no candidato. A redução de hashes não produziu redução de parede:
17559 28,78 → 29,78 s; Charlie 10,47 → 13,29 s; Moto 12,25 → 14,57 s; Castle
8,66 → 11,42 s (`optimized-*`). O custo extra de metadados/nomes canônicos
superou o ganho de I/O. As tabelas por hash reduziram esse overhead. A troca
isolada do provedor de SHA dos inventários não comprovou eliminação dos cerca
de quatro segundos da leitura do catálogo; não se atribui todo esse custo ao SHA.
A rodada intermediária `direct-path-*` registrou ganhos de 24,14% / 12,67% /
12,08% / 7,83%. Após as correções finais de preparação, a rodada completa foi
repetida; a tabela principal usa os números finais, inclusive o ganho menor de
Castle, sem escolher o melhor resultado de cada rodada.

Na primeira versão, o no-op estável deu 17559 20,55 → 18,80 s; Charlie 18,54 →
16,26 s; Moto 21,47 → 22,41 s; Castle 17,90 → 19,48 s. As operações de leitura
caíram fortemente, mas não houve ganho consistente de parede. Esse resultado
motivou a segunda otimização dos loops de recibos; não é apresentado como
aceleração comprovada dos quatro no-ops.

O profiling da toolchain encontrou aproximadamente 7 s internos: 0,71 s para
ler o snapshot/dep database, 1,34 s para compdb, 0,66 s para variáveis CMake,
0,72 s para diretórios de dependências, 2,59 s na descoberta restante de comandos
e 0,92 s em inventários. Essa descoberta não foi removida ou transformada em
aprovação persistente. O restante do no-op inclui host/devshell e autenticação.

Uma primeira tentativa de no-op 17559 reconstruiu conservadoramente em 583,27 s;
a repetição estável reutilizou o recibo em 18,80 s. A causa exata da primeira
invalidação não foi capturada; o tempo não entra na tabela de no-op. Durante essa
recuperação secundária houve testes sintéticos/build do frontend, portanto ela
também não é usada como comparação de rebuild completo.

A primeira recertificação dos loops compilados foi recusada em 177,98 s porque
um input mudou durante a preparação. Nenhum recibo foi publicado. Os 4.013 inputs
recolhidos em uma sonda estável posterior tiveram hashes compilados iguais à
referência PowerShell, sem diferenças. A recuperação conserva o marcador e
executa rebuild seguro; não se autenticam intermediários de uma tentativa falha.
`X360N_PREPARATION_DIAGNOSTICS=1` habilita motivos de rejeição e arquivos que
divergem dos snapshots quando o gate final falha; fica desligado normalmente.

O ReXGlue pinado regrava o `sdk_version` do manifesto mesmo quando não muda,
atualizando seu timestamp; esses manifestos integram `CMAKE_CONFIGURE_DEPENDS`.
Também foi observado Ninja regenerando CMake em uma recertificação, recompilando
o produtor e exigindo codegen. O diagnóstico posterior capturou `rex/version.h`
alterado **depois** do snapshot de compilação: o stamp regravado acionava CMake
durante `4-build`. A primeira restauração ocorria tarde demais; foi movida também
para o fim do codegen explícito, antes do Ninja. Mantém-se o gate final de SHA.

O primeiro fallback compatível com .NET moderno também selecionava
`SHA256.Create()` no Framework, pois o nome parcial de assembly não resolvia CNG.
A recuperação dessa versão terminou em 592,82 s, mas não representa a versão
final rápida. A resolução agora usa a identidade completa do assembly Framework;
um teste exige CNG no PowerShell 5.1, e o fallback moderno continua passando pelos
mesmos vetores SHA-256. Não houve diferença de digest nos 4.013 inputs reais.

A primeira normalização rápida preservava a grafia fornecida em vez da grafia
canônica em aliases de caixa. Uma regressão dedicada reproduziu duas recusas.
O cache final obtém o nome pelo handle protegido e o compartilha com a leitura
de hash, preservando aliases, caminhos longos e Unicode. A suíte focada completa
passou após essa correção; não se manteve a otimização semanticamente divergente.

## Resultados negativos preservados

Trocar apenas `SHA256Managed` por CNG com `ComputeHash(Stream)` não trouxe ganho
relevante: 17559 4,34 → 4,38 s; Moto 4,75 → 4,76 s; Castle 3,63 → 3,51 s;
Charlie 4,15 → 4,07 s. O ganho exige também aumentar os blocos de leitura.

A primeira versão compilada do seletor reabria cada dependência repetida e
levou 88,40 s contra 58,83 s no PowerShell. Foi corrigida com reutilização apenas
sob retenção de arquivos/ancestrais. Comparação seguinte: 1.072 produtos, hashes
e assinaturas idênticos; núcleo 66,05 → 8,17 s; incluindo carga/consultas 18,18 s.
Esses números são da fase de checkpoint, não de uma preparação inteira.
Conferência adicional no Motocross: 2.369 nós e 3.364 dependências de compilação
idênticos; 1.166 produtos com hashes/assinaturas idênticos. Núcleo 68,26 → 10,75 s
(84,25%). O tempo total desse ensaio inclui a própria comparação dos inventários
com o parser anterior, portanto não é usado como tempo do driver novo.

## Testes e limites

As regressões focam adulteração com timestamp preservado, inventários, isolamento,
publicação atômica, flags/response files, headers, objetos/PCH, cancelamento,
timeout, encerramento e sessões consecutivas. Escrita e rename ficam excluídos
somente durante a transação e voltam a funcionar depois. Há teste de mapping
gravável e de junction. Não se executa suíte completa do SDK.

Rodada C++ de `4df2339`: launcher **17 casos / 173 asserções**; contratos de supervisor
**27 / 1.108**; catálogo **85 / 53.878**, todos aprovados. Duração dos três targets
CTest: 7,53 / 62,99 / 13,86 s. Incluem aliases de caixa, Unicode, caminhos longos,
cancelamento, timeout, sessões consecutivas e fechamento de processo sintético.
Os scripts focados de recibos, proteção de produtos e seleção incremental passaram;
recibos/helper/metadata também passaram no PowerShell 7. O teste de toolchain
passou com shadow headers, bibliotecas e ambiente alterados. Não há teste de
gameplay, nem nova certificação funcional da dashboard ou dos jogos.
Após as correções tardias: catálogo **86 casos / 53.899 asserções**, aprovado em
17,89 s; proteção/cancelamento **7 / 46**, aprovado. O fixture
`aot_module_paths_test.ps1` passou; a verificação local de geração idempotente
passou com **14 asserções**, preservando recibo, conteúdo, catálogo e saves.

O linker inicialmente não pôde substituir o launcher em uso. Foi criado um build
isolado em `.local/build/startup-performance`; nenhum processo do usuário foi
encerrado. A primeira configuração desse diretório sem `x86-64-v2` encontrou os
intrinsics SSSE3 do SDK; as flags foram alinhadas ao frontend anterior antes dos
testes/comparações válidos.

## Procedimento manual para startup real

1. Preparar os quatro títulos pelo driver oficial e confirmar ausência de
   marcadores incompletos. Usar a mesma configuração, conteúdo, renderer Xenos,
   perfil e condição inicial dos caches em cada par. Não misturar versões.
2. Em um terminal do mantenedor, criar uma pasta de evidência e definir
   `X360N_ACTION_TRACE` para ela e `X360N_STARTUP_METRICS=1`. Abrir manualmente o
   launcher atualizado. Essas variáveis são opcionais e ficam desligadas no uso
   normal. Não editar manifestos ou recibos para habilitar a coleta.
3. Clicar manualmente em Iniciar. Os TSVs por PID distinguem comando recebido,
   criação do supervisor, `launch.catalog-preflight`, criação do processo do
   título, criação do frame nativo e `session.title_window.attached`. O frame
   persistente vazio não comprova a primeira janela/imagem do título.
4. Cada transição grava `startup.csv` em seu próprio diretório de run. Sua primeira
   linha completa mede `Runtime::Setup` → primeiro swap registrado. Validar
   `swap_failures`, logs e imagem original observada. CSV de swaps não comprova
   apresentação correta nem gameplay pronto. Flush é bufferizado; não inferir o
   instante do primeiro swap pela hora em que a linha apareceu no disco.
5. Para separar carregador/DLL, ReXGlue e apresentação com precisão entre relógios,
   coletar manualmente ETW/WPR de processo, loader, disco e D3D12 no mesmo ensaio.
   Os CSVs atuais fornecem tempo interno relativo ao runtime; não inventar
   alinhamento exato com a janela a partir do timestamp de criação do CSV.
6. Encerrar pela janela, aguardar a liberação efetiva e iniciar outra sessão.
   Fazer pares frios após reinicialização do SO e pares quentes consecutivos,
   alternando ordem e preservando a mesma condição inicial de perfis/caches.
   Este trabalho não purga cache global nem executa esses jogos automaticamente.
7. Guardar TSV, CSV, logs, recibos/hashes, condição fria/quente, versão do driver
   gráfico e observação manual da primeira imagem. Resumir TSVs com
   `scripts/performance/summarize-launcher-trace.ps1`. Remover as duas variáveis
   de diagnóstico ao terminar.

Tempos clique → processo real, processo → janela guest e janela → imagem original
permanecem dependentes desse teste manual. Não há ganho de FPS alegado.

Uma preparação nativa completa não equivale à primeira importação com caches de
análise PPC e disco vazios. Esse cenário inicial e pares realmente frios após
reinicialização do SO não foram medidos automaticamente.

## Commits locais e reprodução

- `f11faaa` — fingerprints compilados e reutilização autenticada de AOT.
- `dc6e09a` — loops de recibos compilados, sem cache de aprovação.
- `7beab02` — diagnóstico opt-in de divergências de inputs.
- `49dc4d6` — metadados de codegen idempotente e provedor CNG correto.
- `f6cbe03` — transação de validação compartilhada no supervisor e benchmarks.
- `98a915d` — aliases canônicos e prevenção da regeneração tardia de CMake.
- `9d58914` — escopo de resolução protegida exclusivo do lançamento.
- `4df2339` — tabelas por hash e SHA nativo de inventários nesse escopo.
- `965db6b` — nomes já protegidos sem nova normalização e inventário por hash.
- `d4d640d` — targets AOT genéricos compactos e teste de dependências/no-op.
- `d48fdfe` — geração de projeto idempotente, marcação antes de escrita real e cancelamento aninhado.

Os benchmarks são targets opcionais (`EXCLUDE_FROM_ALL`). Na configuração
desktop de testes, construir `x360n_preparation_benchmark` e
`x360n_launch_benchmark` sempre via `scripts/devshell.cmd`. O primeiro recebe
`<root> <id> <nova-pasta-de-evidência>` e executa apenas o driver oficial. O segundo
recebe `<root> <id> shared` e somente valida. Para reproduzir a comparação antiga,
extrair `src/supervisor/digest_batch.cpp` e `preparation.cpp` de `c84a4b4` para uma
pasta de evidência com nomes `digest_batch-before.cpp` e `preparation-before.cpp`;
configurar `X360N_BASELINE_CORE_DIR` apontando para ela, com as mesmas flags, e
construir `x360n_launch_baseline`. Usar `split` nesse executável. Não confundir
as variantes de um único executável com o baseline original de código.

Regressões de scripts: `tests/build_isolation/incremental_test.ps1`,
`product_protection_test.ps1`, `toolchain_snapshot_test.ps1` e
`tests/supervisor/preparation_receipt_test.ps1`, em Windows PowerShell 5.1,
com o Ninja pinado explícito no teste incremental. Os três targets CTest usados
são `desktop.launcher`, `desktop.launcher-contracts` e `desktop.catalog`;
selecionar somente esses targets afetados, sem suíte completa do SDK.
