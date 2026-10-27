# Forza Horizon 2 e Minecraft — suporte genérico

Escopo: fazer os dois jogos locais funcionarem sem ajustes por título.
Preservar o SDK pinado, a semântica PPC/XAM, os originais e os dados de usuário.

## Preparação: bloqueio identificado em 25/10

Forza Horizon 2, Title ID `4D530AA4`, versão `0.0.0.12`, XEX SHA-256
`516b04d7c69313cc581062e44030af3636f689e7f09eed1a709e4319f031feb7`:
a geração falhava em `Unsupported AOT dependency identity: $systemupdate/avatareditor.xex`.
O gerador examinava aplicações e módulos do firmware contidos no disco como
dependências do jogo. A correção genérica `e407eb8` exclui somente a pasta
reservada `$SystemUpdate` na raiz da descoberta de módulos AOT, preservando
todos os arquivos e hashes em `assets.sha256`.

O teste sintético reproduziu a falha antes da correção. Depois, o filtro
`[generic-aot]` passou 114 verificações em quatro casos. Inclui DLLs de jogo,
basenames repetidos no firmware, payload de firmware não analisável e uma pasta
de mesmo nome dentro de `assets/`, que continua sujeita à análise normal.
O build foi feito via `scripts/devshell.cmd`, em Release. A publicação atômica
dos temporários falhou no sandbox; os testes foram repetidos fora dele.

A geração real do Forza passou 12 verificações e declarou somente as duas DLLs
do jogo: `SpeechFacade_default.xex` e `XMediaFacade_default.xex`. Não foram
acrescentados perfis manuais ou hints de funções.

## Minecraft local

O catálogo não continha Minecraft na retomada inicial. O pacote local
`584111F7/000D0000/49AAD81B9FCDA45E4A03D71BFCB353F8FADB236C58` foi reimportado
pelo fluxo existente de extração/importação e geração AOT. Versão `0.0.0.1`,
XEX SHA-256 `6d79bb823091890dabbe586e15c0744f707565c980ad998eaf43c808731308d1`.
Importação e geração passaram oito verificações. Trata-se do executável do
pacote do jogo; o bootstrap e o Title Update presentes no disco são distintos.

## Retomada de 26/10

A preparação anterior do Forza foi interrompida durante a compilação da
ferramenta SDK, sem recibo final. Foi retomada pelo driver oficial, com limite
de 40 minutos e acompanhamento. A geração de projeto não comprova gameplay.
O usuário autorizou testes visuais e Computer Use nestes dois jogos, com cópias
isoladas dos dados e limite de três minutos por tentativa.

A primeira retomada restrita passou configure (9,1 s) e ligou `rexglue.exe`,
mas permaneceu no wrapper PowerShell do Ninja após o término dos processos
de compilação. Consulta mínima `--version` e reprodução com 500.030 caracteres
de stdout terminaram com saída 0. A causa ainda não foi demonstrada; não foi
alterado o guard nem relaxada a certificação. A tentativa foi interrompida pelo
controle da sessão e a preparação foi repetida fora do sandbox, com novos logs.

Evidências privadas: `.local/evidence/generic-games-2026-10-25/` e os logs
`x360n-build-*` nas árvores `.local/build/aot/ga4cd45215ceb5054` e
`.local/build/aot/gf107b6bf6a6a3256`.

## Descoberta de saltos diretos — 0203

Fora do sandbox, a ferramenta compilou em 148 s e o codegen do Forza falhou em
104 s: seis destinos de `b` não pertenciam a função alguma (`82C9EFD8`,
`82C9EFF0`, `82C9EFF8`, `82C9F000`, `82C9F008`, `82C9F010`). O desassembly
confirmou getters e adjustors adjacentes, sem `.pdata`, depois de um wrapper
que termina em salto de cauda. O preenchimento linear do gap registrava só o
wrapper e perdia o código que não era alcançado por ele.

O teste sintético reproduziu três entradas ausentes e a falha de validação.
O patch reproduzível `0203-unowned-direct-tail-discovery.patch` faz a descoberta
seguir destinos explícitos de saltos diretos incondicionais ainda sem dono,
após determinar a propriedade dos gaps. Não promove dados, trechos já cobertos
por outra função ou buracos protegidos por `.pdata`/extensões declaradas. Usa
o limite existente de iterações. Não acrescenta hints por jogo, não altera o
runtime e não mascara erros remanescentes.

Aplicação pelo script oficial e verificação de hashes aprovadas. Build Release
focado aprovado; `[gapfill]`: **159 verificações / sete casos aprovados**,
incluindo limites de `.pdata`, extensões explícitas, switches manuais e dados.
A reanálise real permanece necessária para demonstrar avanço dos jogos.

Com 0203, o codegen real terminou com saída 0 em 126,266 s (run
`20261026T221110Z-fh2-codegen-0203`), superando as seis falhas iniciais.
A emissão ainda avisou sobre o destino `831D75A0` absorvido por um gap iniciado
em `831D7590`. Esse destino contém o salvamento/restauro original de contexto;
sucesso do comando não torna essa chamada válida no produto.

O segundo teste sintético reproduziu a ausência de uma entrada referenciada
explicitamente dentro de um gap especulativo. O patch `0204` antecipa a mesma
descoberta de destinos para antes do preenchimento dos gaps. A entrada passa
a ser independente, e o caminho que chega por fall-through continua executando
o corpo original. **170 verificações / oito casos `[gapfill]` aprovados**.
As preparações oficiais dos dois jogos foram retomadas com os dois patches.

Avisos adicionais preservados: função gerada com 2.535.364 bytes e dois `bdz`
cujo destino aponta para padding nulo no fim de `.pdata`. Não foram alterados
por hipótese de causa de gameplay, nem apresentados como sucesso funcional.

Com 0203/0204, o codegen oficial do Forza terminou em 141,9 s: 31 arquivos
escritos, 612 inalterados. O aviso de chamada sem destino para `831D75A0`
desapareceu. Compilação nativa e certificação ainda em andamento.

Minecraft concluiu o driver oficial em Release, com saída 0 e recibo de
**4.159 inputs / quatro artefatos**. Etapas: configure 415 s, ferramenta 173 s,
codegen 49 s, reconfigure 17 s, build 184 s. O marcador incompleto foi removido
pelo driver. Os dois avisos de `bdz` no fim de `.pdata` também permanecem no
Minecraft. Não houve execução ou aceite de gameplay até esta certificação.

## Primeiras execuções limitadas

Os dois produtos receberam recibos pelo driver oficial antes de executar.
Cada tentativa usou Xenos/D3D12, limite de 180 s, captura de pilha e uma cópia
dos perfis/armazenamento. Os **28 arquivos originais usados como seed** foram
conferidos por SHA-256 e preservados em ambas as tentativas.

* `20261026T223315Z-minecraft-generic-1`: abortou em 31.656 ms, antes do menu,
  por chamada indireta não registrada em `8233F020` (`LR=8233EEF0`). O endereço
  aparece numa palavra de `.rdata` em `82012C14`; seu corpo é frameless, sem
  `.pdata`, depois de um adjustor que termina em `b`.
* `20261026T223409Z-fh2-generic-1`: abortou em 29.764 ms, antes do menu,
  por chamada indireta não registrada em `83243750` (`LR=82BFFAA0`). A referência
  original está em `.data`, `83303680`; trata-se de outro callback frameless
  adjacente a um salto de cauda.

As janelas desapareceram antes de uma observação visual concluída. Não houve
entrada de jogo bem-sucedida por Computer Use, imagem de menu ou gameplay.

## Referências de dados sem RTTI — 0205

O scanner de vtables do SDK exige RTTI/COL. Os dois callbacks acima mostram a
limitação mais geral: entradas válidas alcançadas somente por palavras de dados.
O patch 0205 semeia referências alinhadas de seções não executáveis para código
sem proprietário, respeitando metadados e exigindo fronteira após terminador
incondicional/padding e prefixo válido limitado a 64 palavras. Não adivinha o
tamanho do corpo: a descoberta CFG normal continua responsável pelos sucessores.
Os limites da seção e da imagem restringem a leitura. Nenhum endereço ou hash de
jogo foi acrescentado ao compilador ou aos manifests.

O teste sintético reproduziu o callback ausente antes do patch. Com ele, os
dez casos `[gapfill]` passaram **200 verificações**, incluindo `.rdata`, `.data`,
alinhamento, referências para dados, interiores de metadados e fall-through.
Os dois produtos concluíram Release/recibo com esse produtor. Os callbacks
anteriores aparecem nas tabelas AOT e deixaram de ser a falha de inicialização.

## Segunda execução e resolução de exports — 0206

`20261026T225807Z-minecraft-generic-2` caiu após 12.079 ms em leitura guest
`000001A4`. A tabela de pares PPCFuncMappings permite associar o topo nativo
RVA `017FA025` à função guest `8279FA88`, sem símbolos de debug. Isso localiza
a leitura; ainda não comprova a origem do ponteiro inválido. O host genérico
passa a aceitar `x360n_guest_fault_context`, vazio por padrão, reutilizando o
observador limitado existente que não trata nem modifica exceções.

`20261026T225831Z-fh2-generic-2` carregou XMediaFacade original (3.212 funções)
e caiu após 15.050 ms. O runtime esgotou a reserva de thunks do módulo principal
ao resolver o ordinal 6 dessa DLL, terminando com CTR zero. Cada consulta de
UserModule alocava outro thunk, inclusive no despacho host que aceita o endereço
guest original. O teste sintético pelo mesmo UserModule reproduziu cinco falhas:
identidade, repetição, mesmo módulo, binding sobrevivente ao unload e reload.

0206 mantém o endereço original para chamadores host/mesmo módulo, reutiliza
um thunk por export/chamador legado e invalida/reutiliza esses slots quando o
exporter é descarregado. Asserções adicionais cobrem isolamento dos chamadores.
Patch, base e resultados estão registrados no lock. Replay independente dos
três arquivos produziu os SHA-256 esperados. Exports, despacho host e contexto
de falha passaram **139 verificações em quatro casos focados**; o observador
mantém a exceção original, rejeita sobrescrever evidência e limita os registros.
As preparações oficiais estão sendo renovadas com 0206 e o host compartilhado.
Não houve menu/gameplay demonstrado. Os 28 arquivos seed originais continuaram
preservados nas duas tentativas, com armazenamento isolado e limite de 180 s.

## Minecraft: contexto da falha e salto não local

Preparo 0206 `3e6902b39df645bd8f7e32fa00480f75`: Release/recibo concluídos,
etapas 13/141/38/11/168 s. A tentativa `20261026T232457Z-minecraft-generic-3`
reproduziu a leitura `000001A4` após 12.682 ms, sem menu; seed 28/28 preservado.
O observador registrou `r3=1`, `r4=24000848`, `r5=826F78CC`, `LR=0`, `r31=0`
e SP alterado enquanto a pilha C++ permanecia no decodificador JPEG.

A conferência do código original identificou o par CRT `8268DF40` (setjmp)
e `8268DC20` (longjmp). `826F78C8` salva o ambiente; o callback de erro JPEG
chama longjmp em `826F4D98`. A rotina restaura SP, CR, LR e registradores guest,
mas a chamada C++ comum retorna ao JPEG antigo, sem desfazer a pilha host.
É a mesma classe de limitação anteriormente tratada por endereços explícitos
na dashboard, agora exigindo identificação automática para o preparo genérico.
O candidato 0207 reconhecerá o corpo ABI completo em código executável e
selecionará os hooks nativos já existentes, mantendo os campos manuais vazios.
Fixtures sintéticos incluem prefixo opcional, slots/VMX divergentes, seção
truncada, dados não executáveis e cópias independentes. Antes do patch: dez
falhas/75 verificações. Após aplicar pelo mecanismo oficial, CRT + fronteiras
passaram **275 verificações em 11 casos**. Nenhum campo manual de endereço foi
preenchido. Novos executáveis e comportamento após salto continuam pendentes.

## Forza após exports estáveis

O preparo `558f382ad2bd4ab6bdedcf109f0a72da` concluiu Release/recibo, com etapas
12/137/113/13/316 s. A tentativa `20261026T233802Z-fh2-generic-3` superou a
exaustão anterior e caiu após 13.858 ms em callback `82489A30`, LR `8245F9BC`.
Trata-se de thunk frameless para chamada virtual, carregado por `lis r11` em
`825FC56C` e `addi r4,r11` em `825FC57C`, sem ponteiro literal nos dados.
O scanner amplo de lis/addi existente está desativado por falsos positivos.
A investigação usará referências restritas a um bloco conhecido, sem clobber,
com as mesmas proteções de entrada/corpo/metadados já validadas em 0205.
Seed original novamente 28/28 preservado. Não houve aceite de menu/gameplay.

A ferramenta de Computer Use recusou a seleção da janela com a mensagem
`Computer Use was not approved to use ga4cd452...`, apesar da autorização
registrada no chat. A interação visual foi interrompida; nenhum input foi
enviado ao jogo. Não se contornou o bloqueio com outro mecanismo de captura.

0208 usa somente blocos já descobertos: procura lis até oito instruções antes
de addi/ori, preserva a origem por whitelist conservadora e reutiliza as guardas
de candidato de 0205. Não habilita o scanner amplo desativado. O fixture reproduz
três ausências antes do patch e preserva os controles negativos (clobber, bloco,
metadados, fall-through e dados). **347 verificações em 12 casos** de CRT,
fronteiras e callbacks passaram; replay independente de 0207/0208 conferiu todos
os hashes finais. Os preparos oficiais dos dois jogos estão sendo renovados.

## Correção do falso positivo em tabelas embutidas — 0209

O preparo Minecraft `9c4c987c83ef420ab1988eea4548ecb6` compilou/certificou
(12/131/38/11/143 s), mas introduziu 25 avisos de ramos condicionais sem destino
além dos dois bdz anteriores. **Esse produto não foi executado.** A sonda sem
emissão mostrou entradas falsas em tabelas `.text`, como `826636B4` e `823CB4A4`:
os ponteiros da tabela decodificam como instruções e o primeiro case termina
em blr. Uma função descoberta sem pdata não possui os bytes da tabela no seu
conjunto de blocos, permitindo o falso positivo de 0208.

0209 preserva no grafo o tamanho exato dos slots lidos (absolutos, bytes ou
halfwords) e exclui esses intervalos da descoberta de entradas. O fixture
inteiramente sintético reproduziu a tabela tratada como função antes do patch.
Depois: **360 verificações/13 casos** de CRT/fronteiras/callbacks e **234/4** de
despacho CTR aprovadas. Replay independente conferiu os três hashes finais.
O SDK continua pinado; validação e expectativas não foram afrouxadas. O produto
será renovado e os avisos conferidos antes de outra tentativa do jogo.

Na renovação `7faacd199c31433eaab1b7f9063d78c3`, o codegen real concluiu em
35,0 s, com 19 arquivos escritos/250 inalterados. **Zero avisos de ramo
condicional sem destino**; os 25 novos avisos desapareceram. Só permanecem
os dois bdz anteriores para padding. Os hooks nativos do CRT continuam emitidos.
A compilação/certificação desse produto ainda está em andamento.

O driver concluiu Release/recibo com 0209, etapas 11/127/38/11/145 s.
Tentativa `20261027T025802Z-minecraft-generic-4`: o supervisor encerrou pelo
limite de **180.185 ms**, sem exceção não tratada, chamada não registrada,
stub ou mensagem não implementada. O arquivo opcional de contexto ficou vazio.
O log avançou pelo carregamento dos recursos; vários mipmaps opcionais ausentes
foram registrados (os arquivos base existem no acervo). Permaneceram os dois
erros de dispositivo devkit/CdRom0. **Sem observação visual ou input: timeout
não comprova menu ou gameplay.** Seed original 28/28 preservado.
O preparo oficial do Forza foi iniciado com o mesmo conjunto de patches.

Replay completo do estado atual a partir de
`f5337cdc947ff6d4c4196737e2c807a48f2a1fc2`: **196 patches, 267 arquivos,
zero divergências de SHA-256**, 76,0 s. Os hashes-base dos novos patches
0203–0209 também foram conferidos antes de cada aplicação. Evidência local:
`.local/evidence/generic-games-2026-10-26/patch-replay-90ae952ad2764bb5b92c9cd59a8c320f.json`.
Não é teste de gameplay nem repetição das suítes de títulos aprovados.

O codegen Forza `591598cae49546f897a4210c01363d7a` terminou em 99,6 s, com
71 arquivos escritos/572 inalterados, incluindo os módulos originais do jogo.
O callback `82489A30` agora está na tabela PPCFuncMappings sem hint manual.
A compilação nativa/certificação seguem em andamento.

O driver Forza concluiu Release/recibo com etapas 11/139/103/12/282 s.
Tentativa `20261027T031306Z-fh2-generic-4`: encerrada pelo supervisor no limite
de **180.248 ms**, sem exceção não tratada nem chamada não registrada. Contexto
de falha vazio, seed original 28/28 preservado. Permaneceram quatro erros
(\Device, cache, update e app F1) e o stub ObReferenceObject; não foram ocultados
nem transformados em sucesso. A ausência de crash não demonstra que esses
contratos sejam suficientes para gameplay.

Estado ao final da etapa: **dois produtos preparados e duas tentativas limitadas
sem crash, mas nenhum aceite visual de menu ou gameplay**. O acesso ao aplicativo
foi recusado pela ferramenta de Computer Use; a solicitação específica de
liberação foi enviada e permanece pendente. A autorização do usuário no chat
continua registrada. Não foram tentados outros mecanismos para observar ou
controlar a janela. Nenhuma sessão de jogo ficou aberta após o limite.
Medições de build/codegen são observações de diagnóstico; não há ganho de FPS,
latência ou performance de gameplay demonstrado. A matriz de compatibilidade
não foi promovida a jogável.

Retomada de 27/10: a liberação específica do Computer Use foi concedida. Minecraft
mostrou menu e mundo 3D do tutorial na tentativa `20261027T102550Z-minecraft-visual-5`;
movimentação e interação continuam sem comprovação. Forza mostrou a abertura e
o aviso “No Device Selected” (`20261027T103424Z-fh2-storage-6`). O diagnóstico
direto omitia o contexto de console; o host genérico já aceita os serviços
originais da dashboard, utilizados nas transições normais de sessão.

`SessionPlatformArguments` agora centraliza identidade, HDD, versão, UI e avatar
para a sessão e o diagnóstico direto. Este passa a usar o HDD da cópia da
dashboard e aceita duração explícita de 1–180 segundos (padrão 30). Manifestos
de jogos não podem substituir `content_data_root`. Release e testes focados:
**32.125 verificações/12 casos**. Não houve seleção automática de dispositivo,
mudança de licença, fallback de versão ou ajuste por título.

O preparo oficial `dash_17559` (`4745e524cd0a40fd8a1d46aed6fc44e3`) renovou
ferramentas e codegen, mas foi encerrado antes do recibo final após o Computer
Use informar interrupção física por Esc. Marcador incompleto preservado;
não utilizar o serviço antigo nem retirar o marcador manualmente. Nenhum jogo
foi iniciado com o contexto corrigido. As tentativas anteriores preservaram os
28 arquivos originais do seed. Falta concluir o preparo e comprovar a seleção
original de armazenamento e o gameplay dos dois jogos.

Retomada posterior ao Esc: o usuário esclareceu que o trabalho de código deve
continuar independentemente do Computer Use. Preparo oficial 17559
`78cdd48166274ed69e41b94f9daaf301` concluído, etapas 12/136/33/12/259 s,
sem executar a dashboard. O diagnóstico registra a biblioteca somente no HDD
descartável e conserva a verificação integral de conteúdos/recibos. Um hint de
registro existente evita escritas redundantes, mas não autoriza lançamento:
`VerifyCatalogLaunch` ainda confere todos os hashes. Xenos/D3D12 é override
explícito do diagnóstico; `graphics.toml` do usuário (Native) não foi alterado.

Tentativa `fh2-console-storage-7`: preflight 113.967 ms, filho 165.412 ms.
Host 17559 e assets originais conferidos; pedido FE/2100B criou/anexou/layoutou
a cena com resultado zero e compôs milhares de quadros. O app original concluiu
o diagnóstico com resultado/extended `0/0`, dispositivo `1`, liberando foco.
Um pedido posterior do próprio jogo também abriu/fechou o seletor. Foram criados
ForzaProfile, GameplayLog, PlayerDatabase e VersionFlags no HDD isolado. Depois
ocorreu AV `C0000005`, leitura guest `0008039D`, sem atingir o timeout.
O usuário informou que a janela fechou antes de concluir seu teste; isso não
é aceite de gameplay ou de save/load comercial.

Na repetição pelo fluxo normal `fh2-profile-fault-8`, sem pedido artificial
do seletor, o mesmo ponto caiu em 16.605 ms, leitura guest `00000368`.
Mapeamento offline da tabela PPCFuncMappings do PE e disassembly original
localizaram `82E2DD2C`: carrega o buffer de configurações, seu ponteiro em +4
e um campo em +872. O observador registrou r11=0; a causa ainda é hipótese.
O caminho anterior chama XamUserReadProfileSettings com 29 IDs padrão. Teste
CPU novo do lote de 4.312 bytes passou, síncrono/assíncrono, preservando header,
ordem e sentinelas: **113 verificações/1 caso**. Um primeiro fixture assíncrono
sem thread guest foi corrigido antes da validação; não foi classificado como
defeito do SDK.

Tentativa `minecraft-console-storage-6`: mesmo HDD descartável, preflight
20.946 ms, encerramento pelo limite em 60.219 ms, sem AV. O seletor original
concluiu cancelamento `1627`, extended `1223`, dispositivo `DEADBEEF` inalterado.
Nenhuma escolha automática ou input de desktop foi usado pelo agente. Os 28
arquivos originais do seed permaneceram iguais nas três tentativas.

O observador opcional agora captura até dezesseis spans legíveis de 64 bytes,
com um único nível de ponteiros, usando ReadProcessMemory para rejeitar páginas
inválidas sem nova exceção. Continua sem tratar falhas ou alterar registradores,
desligado normalmente, com limite anterior de dezesseis registros. **62
verificações/2 casos** aprovadas. Novo Forza Release/recibo
`26441e1180444f17be9b094cca8e9494`: 12/145/103/13/324 s; codegen não mudou os
643 C++ existentes. Recibos dos demais consumidores desse arquivo compartilhado
estão sendo renovados antes de uma nova execução.

17559 renovado em `926845183124489298d4976b0639b06d`, etapas
13/141/33/13/277 s. Tentativa `fh2-profile-memory-9`, pelo fluxo normal:
preflight 13.598 ms, filho 18.142 ms, AV no mesmo getter, guest `00000489`.
O executável exato `4c6f06909e09cb738cd5f7c500afd69aa9b61422d834643e3d3e44cbfca5498a`
e os dois recibos foram registrados no diretório do run. O buffer apontado
por objeto+44 começava com `FFFF0040 00000121`, em vez de contagem/ponteiro
válidos de X_USER_READ_PROFILE_SETTINGS. Isso demonstra os dados inválidos no
momento da leitura, não sua causa ou quem os escreveu. Originais 28/28 preservados.

0210 acrescenta registros DEBUG de sizing, escrita bem-sucedida e resultados
base/Ex. Só usa argumentos e valores que a própria API já validou/escreveu;
não lê dados extras do chamador depois da conclusão, não altera buffers ou
códigos e não registra valores de perfil. A macro de log impede avaliação e
formatação em INFO. A prova sintética falhou 6/18 antes do patch; depois,
**311 verificações/8 casos** de configurações/sizing passaram. Replay completo
**197 patches/267 arquivos**, zero divergências, 61,1 s; evidência local
`patch-replay-af87764a56a043799fab78124b526ba4.json`. Novo produto está em preparo
para distinguir o retorno XAM das alterações posteriores no guest. Não é
correção confirmada da queda nem aceite de gameplay.

Preparo com 0210 concluído: Forza `77b1400ea6854b508ef7e2628b4ef809`,
17559 `3f4ca02008574b9d8fe9f0c0fb3e02db`. Tentativa fresca
`fh2-profile-trace-fresh-10`: preflight 122.364 ms, filho 18.779 ms,
AV guest `001503A1`; executável
`6742d69554375a268920c55c897adce0d5a53142a3f76e8ea5daaddc4e0b4d6b`.
O lote de 29 campos pediu 4.312 bytes, retornou 122 no sizing e 997 na leitura,
sem alcançar a serialização. Portanto o buffer não foi corretamente preenchido
e depois sobrescrito: a API concluiu uma falha assíncrona antes de escrever.

Novo diagnóstico explícito `x360n_profile_settings_probe` carrega apenas os
dados de um XEX autenticado, sem entrypoint, renderer ou input. Copia perfis,
HDD e metadados para um diretório novo e executa a API em uma XHostThread com
limite de dez segundos. IDs/endereço/contagem são argumentos; nenhum ID ou
endereço de jogo foi acrescentado ao runtime. `profile-settings-cpu-1` reproduziu
o erro usando a cópia da tentativa 10: todos os campos existiam e estavam
legíveis, mas Totals retornou kNotFound, sync=1168, async=997/result=1168/
extended=80070490, header intocado. O seed continha desbloqueios de um jogo
anterior, enquanto o wrapper copiava apenas contas/perfis/console e excluía
`offline-achievements-v1`, necessário para contabilizá-los.

Comparação `profile-settings-cpu-2` com cópia completa do mesmo seed: sync=0,
async=997/result=0/extended=0, header=29/ponteiro válido, 4.312 bytes. Corrigido
o wrapper local para copiar também todos os metadados offline e verificar seus
hashes ao término. Não se apagaram desbloqueios, não se fabricaram totais e
não se alterou a semântica XAM para contornar uma cópia incompleta do diagnóstico.
As tentativas 7–10 não certificam o fluxo com console completo; seus erros e
evidências continuam preservados.

`fh2-complete-console-11` confirmou o lote de 29 campos serializado com sucesso
no próprio jogo. A captura pelo Computer Use mostrou céu 3D e “CARREGANDO...
AGUARDE”, além da abertura anterior. Preflight 114.903 ms, filho 44.223 ms,
AV guest zero em outro ponto; 37/37 arquivos do seed completo preservados.
O mapeamento do mesmo executável identifica função `831F4E68`, pós-retorno
da troca de fiber (leitura original `831F4FC8` de r20 nulo). A pilha passa
pela compressão do perfil e o SwitchToFiber original salva/restaura todos os
registradores e chama KeSetCurrentStackPointers, mas retorna à continuação C++
anterior. O SDK já possui hooks de fibers nativas; a configuração genérica ainda
não os seleciona automaticamente. A correção em desenvolvimento reconhece o
ciclo CRT completo, ligado e limitado às seções executáveis, sem endereços de
título. Não é erro remanescente da leitura XAM nem aceite de gameplay.

Minecraft com 0210 concluiu preparo `01e0cbfe3aeb4d3a8917db982622e5f3`
(13/163/39/12/185 s). `minecraft-complete-console-7`, executável
`02907e913f1c12566f0953c1b71f9573d4332beb8501d6fd481151ed12760c49`:
preflight 20.987 ms, filho 180.258 ms, encerrado pelo supervisor no limite,
sem crash capturado; seed completo 37/37 preservado. Computer Use observou o
aviso de salvamento, o menu em português e o mundo do tutorial com deslocamento,
extração de terra e uso da vara de pesca (boia na água e item na hotbar).
Foram enviados Return, Space, W e P, sempre ao processo genérico autorizado;
o deslocamento entre capturas também pode incluir controle físico do usuário,
portanto não se atribui cada mudança a uma tecla específica. A captura inicial
de uma janela ocluída mostrou outra superfície; nenhum input foi enviado a ela,
e a captura foi recuperada ativando a janela exata retornada do jogo.
É evidência de gameplay local do tutorial nesta versão/build, sem certificar
save/reload, versão integral, multiplayer ou o próximo produto com 0211.

0211 aplicado pelo fluxo oficial após staging: `[crt-fiber-discovery]` falhou
10/112 verificações antes e passou 112/112 depois. Controles compartilhados
`[gapfill],[nonlocal-jumps],[ctr-dispatch],[crt-fiber-discovery]`: **706/18**.
Probe CPU nativo realizou quatro idas e voltas entre fibers, preservando suas
continuações, pilhas e GPR/FPR/VMX, e liberou o ciclo de vida. Uma primeira
compilação do probe usava macros exclusivas do código gerado; acessores BE
normais corrigiram o fixture antes da execução. SDK de runtime não foi alterado.
Replay **198 patches/267 arquivos**, zero divergências, 77,8 s,
`patch-replay-6e600ba5fab1414780acd33583ac44c6.json`.

Análise estática real `forza2-fibers-0211.tsv`: resultado ok, seis entradas
inferidas (cinco wrappers e corpo direto do switch), sem `[rexcrt]` ou hints
no manifesto genérico. O patch mantém esses resultados em AnalysisState e
preserva overrides explícitos. Novo preparo oficial Forza iniciado; ainda não
houve execução do jogo com 0211.

Controle posterior de override explícito e separação da configuração do usuário:
**135 verificações de descoberta/9 variantes** aprovadas. Configuração explícita
completa conserva cinco entradas próprias e não recebe hooks inferidos;
configuração genérica permanece vazia, com seis hooks somente no estado da
análise. Isso não reexecutou jogos, regressões visuais de outras versões ou
suítes completas.

No preparo Forza `f83d44044c104a5e8202258c2fc47d0f`, geração oficial com
0211 concluiu em 101,3 s: 36 arquivos escritos, 607 inalterados. A tabela
gerada contém os seis mapeamentos aos hooks nativos; o manifesto não recebeu
`[rexcrt]`. Compilação nativa em curso. Esse tempo de geração não é medição de
ganho de desempenho no gameplay.

Forza com 0211 concluiu Release/recibo `f83d4404…`: etapas oficiais
13/150/105/13/332 s (o cronômetro interno do codegen marcou 101,3 s).
Iniciada renovação oficial do host/serviços 17559 antes do lançamento.

Host 17559 com 0211 concluiu `07e7323c33b443c98beae84679e92cd2`:
14/151/33/13/286 s, mais cinco geradores de UI de 4 s cada; os 428 arquivos
da geração principal permaneceram inalterados. Tentativa isolada
`fh2-native-fibers-12` iniciada com console completo e limite de 180 s.

`fh2-native-fibers-12`, executável
`94fcf468bfea424e1e1a4ed074f11bbbb735766c490568be326c80a135a1eebc`:
preflight 116.969 ms, filho 90.821 ms, nova AV guest `00000004`.
O log confirma Convert/Create/execução/ConvertBack/Delete do ciclo que antes
falhava, além de outra fiber de carregamento concluída. Computer Use observou
a tela de controles do carro depois do carregamento; houve input humano
detectado, sem intervenção do agente. Não chegou a ser demonstrada condução.
37/37 arquivos originais preservados.

A nova pilha é outra thread: `82426800 → 8247DFA8 → 82481E10 → 8245F7F0`.
A instrução original `82426CF4` lê +4 da lista obtida em estado+116, nula.
O caminho monta pacotes de comandos gráficos; a causa da lista ausente ainda
não foi determinada. Não se aplicou bypass, falso sucesso ou alteração do
renderer para contornar esse estado. Captura opcional de memória no supervisor
está sendo preparada para registrar o objeto completo durante a exceção fatal;
ela não executa nem altera código guest.

Captura `ProcessSpec::crash_guest_memory_base`, desligada por padrão, lê até
64 spans de 256 bytes, no máximo 1.024 tentativas, somente na exceção de segunda
chance enquanto o debug API mantém o filho suspenso. Registra GPRs nativos,
raízes legíveis e ponteiros guest plausíveis (GPRs nativos e um nível BE), com
base da arena fornecida explicitamente pelo diagnóstico. Não pressupõe qual
registrador nativo contém PPCContext, não escreve no filho e não altera a
continuação da exceção. **14 verificações/1 caso** com dois filhos sintéticos
e **49/10** controles de processo passaram; modo normal não captura memória.
Um primeiro comando de build pediu um alvo inexistente no build desktop;
corrigido para os targets de cada configuração, ambos compilaram antes dos testes.
Tentativa `fh2-command-memory-13` usa o HDD isolado completo da tentativa 12,
mesmo executável, sem novo preparo do jogo; limite 180 s.

`fh2-command-memory-13`: preflight 13.435 ms, filho 180.195 ms, encerramento
no limite sem crash. Ficou em “Pressione Start”; Return e X enviados por
Computer Use não demonstraram resposta. Portanto não reproduziu a falha de
comandos nem certifica sua correção. Sem captura de memória fatal para analisar;
37/37 originais preservados. Solicitada disponibilidade do usuário para Start
no controle, sem suspender a análise de código. Novo preparo Minecraft iniciado
para renovar seu recibo com 0211; geração será comparada ao build do tutorial.

O usuário informou disponibilidade imediata para Start no controle. A preparação
Minecraft foi interrompida pela árvore de processos identificada, preservando
seu marcador incompleto; deverá concluir novamente o preparo oficial antes de
qualquer lançamento. Não foi executado produto incompleto.

`fh2-command-memory-14`, mesmo executável/console isolado da tentativa 12:
preflight 12.670 ms, filho 32.632 ms, crash C0000409 no ResolveIndirectFunction.
O log registra LR=8245F9BC, destino sem entrada AOT 82460DC8; 37/37 arquivos
originais preservados. A captura opcional gerou 34.846 bytes de evidência.
Esse callback está materializado por lis/addi em um bloco conhecido, mas seu
prefixo contém rldicr, classificado incorretamente como desconhecido pelo
decoder de análise. O emissor nativo já suporta a instrução. Esta é uma falha
distinta da lista gráfica nula da tentativa 12; ainda não se provou relação.

0212 corrige genericamente as quatro rotações imediatas MD: rldicl/rldicr/
rldic/rldimi, seus campos de shift/máscara divididos, leituras/escritas GPR e
CR opcional. Mantém números anteriores dos opcodes, guardas de fronteiras,
tabelas e encodings MDS/reservados desconhecidos. Sem alteração do emissor
aritmético, título ou runtime. O callback sintético falha antes do patch;
controles exaustivos percorrem 32.768 combinações. **767 verificações/19 casos**
focados aprovados; GNU binutils confirma oito codificações independentes,
incluindo 792607E6 do callback real. Uma primeira invocação sem elevação do
binutils foi recusada pelo sistema; a execução autorizada concluiu com saída 0.

`forza2-md-0212.tsv`: callback 82460DC8 recuperado, 85.647 funções,
20.983 entradas sem pdata, 466 tabelas/18.297 slots, zero erros de grafo.
Replay completo: **199 patches/271 arquivos, zero divergências**, 78,6 s,
`patch-replay-cb17d7e13e9f47baa8e2b0f8b6148e6f.json`. Novo preparo oficial
Forza iniciado; o produto anterior não certifica o patch 0212.

Comparação dos grafos 0211/0212: exatamente duas funções novas de 36 bytes,
82460DC8 e 825F3620, ambas wrappers de callback com a mesma instrução rldicr
e salto indireto terminal. A segunda também foi conferida no disassembly
original, fora de pdata e após outro salto terminal. Quantidade e slots das
tabelas permanecem idênticos; os seis hooks CRT também são preservados.

Forza 0212 concluiu preparo oficial `bb0342d245da411f8fb3f95c213853eb`:
14/169/121/14/381 s nas etapas reportadas, todos os retornos zero e marcador
incompleto removido pelo fluxo oficial. Geração: cinco arquivos escritos e
638 inalterados; ambas as novas entradas constam no registro AOT. Renovação
oficial do host/serviços 17559 iniciada antes de novo lançamento.

Host 17559 concluiu `5da0ac427915473b8d332fb831ba05db` com 0212:
15/162/38/15/303 s e cinco geradores UI de quatro segundos; geração principal
zero escritos/428 inalterados. Todos os retornos zero e recibo completo.

`fh2-md-callbacks-15`, executável
`1c65f1775f324651184d346232f14fdf4675f1dd94dc09b404ece4434e90d610`:
preflight 15.167 ms, filho 180.545 ms, encerramento pelo supervisor no limite,
sem crash capturado ou destino indireto ausente; 37/37 arquivos originais
preservados. Reutilizou exclusivamente o console completo isolado da tentativa
12, sem selecionar automaticamente HDD ou modificar o perfil original.
Computer Use observou abertura, controles, introdução e **gameplay com condução**:
“Dirija até o Festival”, minimapa, HUD, 138–206 km/h, marchas 3–5 e distância
9,3 → 8,5 → 6,9 km. Carro e cenário avançaram pelas curvas da estrada costeira.
Input humano foi detectado; também se enviou Return pelo Computer Use durante
a introdução. Não se atribui cada mudança visual a uma tecla do agente.

É evidência de condução inicial neste build genérico, com Xenos D3D12 e sem
hints por título. Não certifica campanha, save/reload, Native ou estabilidade
longa. A leitura de lista nula da tentativa 12 não reapareceu; não se conclui
que sua causa foi identificada. O texto genérico do recibo do harness limita
sua própria observação; as observações visuais acima foram feitas separadamente
pelo Computer Use autorizado. Novo preparo oficial Minecraft iniciado para
concluir o produto antes interrompido e comparar sua geração ao build 0210.

A configuração local global estava em Native experimental. Alinhada ao padrão
validado Xenos D3D12 (`experimental=false`), preservando fullscreen; cópia
anterior em `graphics-before-generic-games.toml` na pasta local de evidências.
É uma escolha única para a biblioteca, sem override por jogo ou alteração do
renderer. O próximo diagnóstico Minecraft carregará essa configuração global
sem a flag de override usada nas tentativas anteriores. Isso não certifica o
backend Native.

Minecraft concluiu preparo oficial `2a48e11c8170452d9557d223726343f5` com
0212: 14/169/45/14/189 s, todas as etapas com retorno zero, marcador incompleto
removido oficialmente. Codegen: zero escritos/269 inalterados. Comparação dos
recibos ao build 0210: **287 fontes geradas em cada, zero adições, remoções ou
hashes diferentes** (inclui fontes auxiliares da preparação).

`minecraft-global-current-8`, executável
`22bde409fcae857ad91f1738550dd785607c6ada5e1e1fbf242f0b80a3390236`:
preflight 5.840 ms, filho 120.207 ms, encerramento no limite, sem crash,
37/37 originais preservados. Usa o console completo isolado da tentativa 7 e
a configuração gráfica global, **sem override Xenos do diagnóstico**.
Computer Use observou o mundo do tutorial com HUD, deslocamento da clareira
até as árvores, extração de madeira e dois blocos na hotbar. Enviados W e P;
também houve input humano, sem atribuir cada mudança visual ao agente.
O jogo exibiu o aviso explícito de avaliação/desbloqueio da versão integral;
não foi solicitado desbloqueio, compra ou alteração de licença.

Ao tentar fechar o aviso, a ferramenta detectou input humano e bloqueou o
comando. A atualização seguinte da janela mostrou uma superfície ocluída por
outro aplicativo; não se enviou input a ela nem se retomou o foco. O restante
do prazo foi acompanhado pelos logs e o supervisor encerrou o jogo. Não houve
interação com aplicativo fora dos dois jogos autorizados.

Estado final desta etapa: **Forza com condução inicial e Minecraft com gameplay
do tutorial demonstrados no produto 0212**, pelo caminho AOT genérico, Xenos
D3D12 e serviços originais 17559. Preparos dos dois jogos e host concluídos;
nenhum processo de teste deixado em execução. As correções são compartilhadas,
sem endereços, configurações CRT ou hacks no manifesto de cada jogo. Campanha
completa, save/reload comercial, licença integral do Minecraft, Native e
estabilidade longa continuam sem certificação. A falha gráfica nula anterior
do Forza não reapareceu no teste de condução, sem causa demonstrada.
