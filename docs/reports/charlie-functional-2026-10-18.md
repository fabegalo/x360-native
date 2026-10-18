# Charlie Murder: primeiro aceite funcional — 18/10/2026

Base: `main` em `507d571`. Esta etapa separa evidência de processo, observação
manual e aceite funcional. Gameplay, renderização XNA e save/load não aprovados.
SDK, 91 patches até 0092, originais e recibos de títulos preservados. Sem outros
jogos iniciados, Computer Use, push ou Release.

## Recibos atuais, sem rebuild de títulos

Revalidados identidade, SDK pinado, bytes de inputs/artefatos/produtor, inventário
CMake, globs, DLLs publicadas e checkpoint autenticado, somente de Charlie e 17559.
Diagnóstico privado: `.local/evidence/charlie-functional-2026-10-18/`.

| Produto | Inputs / artefatos | SHA-256 do recibo |
|---|---|---|
| Charlie, projeto `g0a4c6253b34297dc…` | 4.013 / 35 | `774ad0418479647d96fda91bdcbdeb869fff4d753b2283617ca1179f03374278` |
| Retail 17559 | 4.013 / 13 | `0b6ca96d328977dbb50901d8569bd1a4559c1a529bcafe75db4b6eff530d6915` |

Os mesmos hashes passaram novamente após o build focado do supervisor. Não
houve codegen, rebuild ou emissão manual de recibos de títulos.

## Evidência anterior e primeira falha observada

Sessão já existente em `507d571`:
`20261018T102639Z-session-0-dash_17559` →
`20261018T102725Z-session-1-g0a4c6253b34297dc971555c160fb8affde66f554296bd83bae609` →
`20261018T102744Z-session-2-dash_17559`.

A dashboard pediu a imagem Charlie pelo caminho original `XSYSLAUNCH:`.
O processo Charlie durou **1.431 ms**, exit 0, sem timeout/exceção registrada.
HostLoader registrou 1.461 funções, RuntimeHost 19.633 e NetCFUserMode 2.066.
Depois, NetCF foi descarregado e o próprio título chamou
`XamLoaderLaunchTitle(destination=<dashboard>, flags=00000100)`, produzindo
handoff Dashboard. Esse retorno de inicialização não comprova Guide/Xbox Home.

O mantenedor relatou dois itens: o chamado trial fica preto e retorna; o chamado
completo derruba a sessão. A reprodução manual abaixo esclareceu o encerramento
do segundo item. Há dois headers Charlie, tipos
`00004000` e `000D0000`, com o mesmo nome de conteúdo. Os dois `default.xex`
têm SHA-256 `0e9d4f0967f8dd121c0c93694b601d30cad66d7a87fb4cbb0b9e3325c3d7b4d1`.
O perfil certificado declara `000D0000`, Title ID `58410B83`, versão `0.0.0.2`.
Isso comprova duplicação da imagem de entrada, não duas edições ou licença Full.
Nenhum header, licença ou arquivo instalado foi removido/modificado para mudar
o comportamento.

### Reprodução manual do item chamado completo e correção

Após o mantenedor autorizar abrir a sessão preparada, foi iniciada somente a
dashboard; toda seleção foi manual. A tentativa
`20261018T104054Z-session-0-dash_17559` terminou em 25.692 ms, exit 0 da
dashboard, sem exceção não tratada. Ela pediu o caminho instalado `00004000`.
O supervisor recusou corretamente `no verified AOT profile .../00004000/.../default.xex`
e encerrou a sessão. **Charlie nem iniciou nessa tentativa**: o encerramento
observado não foi um crash XNA. A tentativa inicial `104015Z` foi interrompida
na condução do lançamento; seu log parcial não certifica a dashboard e não é
contado como falha do título.

Causa demonstrada: registro de biblioteca obsoleto sob tipo diferente do perfil
certificado. A importação por pasta não tinha `content_type`; o desktop assumia
`00004000`, enquanto o perfil AOT autenticado declarava `000D0000`. A leitura
do catálogo agora conserva o tipo do perfil gerado vinculado ao mesmo hash de
origem/Title ID/nome de pacote. Metadados de outra origem ou conflito com um
tipo explícito de container exigem regeneração, sem fallback. Esse ajuste não
altera o perfil ou os recibos existentes.

A preparação anterior só examinava o caminho atual, mantendo o outro item
oferecido em My Games. A correção genérica em `library.cpp` procura
registros do mesmo Title ID/nome de pacote em outros tipos **dentro do mesmo
armazenamento da dashboard**. Receitas distintas presentes no catálogo conservam
seus registros. Para retirar um registro obsoleto da enumeração, exige aggregate
exatamente igual ao gerado para aquele tipo e todos os inputs instalados com os
hashes da receita atual, sem arquivos adicionais desconhecidos. Arquiva o header
em `.x360n-inactive-headers` e a cópia obsoleta inteira em
`.x360n-inactive-content`, fora da árvore enumerada, mantendo todos os bytes,
perfis e saves. Metadados diferentes,
trailers de licença, conteúdo divergente/incompleto e redirecionamentos externos
são recusados com preservação. Não há alias de lançamento nem mudança de licença.

O teste sintético no código anterior reproduziu cinco verificações falhas.
A correção passou 56 asserções em quatro casos focados de biblioteca, incluindo
migração, negativos, cancelamento, caminhos longos e registro de receita distinta.
A primeira execução restrita desses testes parou em erro Windows 5 durante o
fixture de importação; foi repetida no ambiente permitido, sem reduzir o teste.

O primeiro ensaio na cópia ainda usou o default do catálogo, expondo a
divergência com o perfil; nenhum original foi alterado. Após alinhar os
metadados, a cópia ficou com um header ativo `000D0000`. A dashboard ainda
mostrou os dois itens a partir do cache persistido, conforme relato manual.
A reconciliação agora arquiva também `ContentCache.pkg/CDIs.bin` compartilhado
e por conta, dentro do armazenamento da própria dashboard, para regeneração
da lista original. Arquivos de cache são preservados byte a byte; headers de
cache, perfis e saves não são apagados.

A sessão de lista `20261018T110054Z-session-0-dash_17559` revelou uma condição
adicional: o mantenedor viu um Charlie e outro item somente com hash. O log
consultou novamente o tipo `00004000`. O SDK enumera também a pasta extraída
sem header; portanto arquivar somente header/cache era insuficiente. A retirada
da cópia obsoleta inteira acima resolve essa origem da enumeração, após conferir
o inventário completo, recusando inclusive arquivos extras de origem desconhecida.
A observação foi encerrada sem repetir o bootstrap do jogo; o log parcial não
aprova nova duração ou navegação de retorno.

**Confirmação final da biblioteca:** em
`20261018T110652Z-session-0-dash_17559`, o mantenedor confirmou **somente um
Charlie Murder, sem o item com hash**. A janela foi fechada pelo usuário após
24.792 ms (`cancelled=true`, sem timeout); o supervisor terminou com saída 0.
Nenhum jogo foi iniciado nessa última conferência. É aceite manual restrito à
lista corrigida, não do runtime XNA. O log continua contendo serviços parciais
da dashboard (2.740 linhas de stub e 761 de erro); não é uma execução limpa.

### Segunda sessão manual: limite real do Charlie e crash posterior

`20261018T105319Z-session-0-dash_17559` pediu o caminho `000D0000`, casou com o
perfil certificado e lançou Charlie em
`20261018T105409Z-session-1-g0a4c6253b34297dc971555c160fb8affde66f554296bd83bae609`.
Sob debugger, durou **6.515 ms**, exit 0 e sem exceção capturada. Os mesmos três
módulos registraram 1.461/19.633/2.066 funções; o bootstrap retornou sozinho,
antes de comprovar o ciclo XNA ou imagem do jogo. Os tempos com debugger não
medem desempenho nem permitem comparação causal com os 1.431 ms anteriores.

A dashboard retornou em `20261018T105434Z-session-2-dash_17559`. O mantenedor
então abriu Storage do sistema e selecionou perfis. Após 52.891 ms, esse
processo da dashboard sofreu `0xC0000005`, lendo guest `0x00000020` em
`sub_921AA480` (`dash_17559_recomp.4.cpp:1070`), após `XamGetDefaultImage STUB`
e `BaseHeap::Alloc page count too big for requested range`. `crash.txt` e
`report.json` preservam a pilha. É falha posterior de navegação em Storage,
não exceção do processo Charlie. A causa interna ainda não foi demonstrada;
nenhum guard/stub foi acrescentado para esconder esse acesso. Não se aprova o
fluxo Guide/Xbox Home com base nesse retorno automático.

O erro `ResolvePath(RuntimeHost.xex) failed - device not found` não demonstra
falha em carregar RuntimeHost: o módulo já está registrado antes dessa linha.
No SDK, `GetModule` consulta o VFS e depois compara os módulos carregados por
`XModule::Matches`, que aceita o nome do módulo. Não atribuir causalidade ao
primeiro erro textual sem verificar esse retorno.

`KeCreateUserMode` e `KeFlushUserModeCurrentTb` aparecem como stubs existentes
durante a inicialização. A análise do código AOT mostra a chamada de criação
com r3=0; o stub não define o retorno e deixa zero aparente. Isso é uma lacuna
real do contrato, mas os logs atuais não provam qual resultado interno provoca
o retorno da CLR. Não foi introduzido sucesso fictício nem implementação
especulativa dessas APIs. A discussão [upstream Xenia #2348](https://github.com/xenia-project/xenia/issues/2348)
é contexto de pesquisa sobre modos de usuário, não especificação validada do
contrato NetCF nem prova da causa desta falha.

## Instrumentação e teste manual preparado

`x360n session` passa a oferecer `--crash-stack` e `--user-data-root` opcionais.
O primeiro reutiliza o debugger existente em `run`; o segundo redireciona as
pastas por título e a identidade compartilhada da dashboard somente na sessão,
sem escrever `user-data-root.txt`. Receipts, versão dos originais e isolamento
17559 continuam verificados. Nenhum botão ou escolha de jogo é automatizado.

Cópia privada em `charlie-functional-2026-10-18/user`: **2.019 arquivos** de
perfis, estado e ambos os registros/conteúdos Charlie conferidos por SHA-256.
Não copiou conteúdo dos demais jogos. Proveniência em `original-user-files.json`
e `copy-result.json`. O roteiro privado `run-manual.ps1` usa limite de 120 s por
processo e confere os arquivos originais ao terminar. Não há aceite de performance:
o debugger acrescenta overhead e os tempos não devem ser comparados aos normais.

Roteiro para cada tentativa, encerrando na primeira falha:

1. Observar intro/Home e perfil selecionado na dashboard 17559.
2. Abrir My Games e anotar qual dos dois itens Charlie foi escolhido.
3. Observar a última tela, áudio, texto e se há animação; marcar o tempo.
4. Se houver tela do jogo, testar uma ação Start/A e anotar a resposta.
5. Se for possível, abrir Guide pelo controle, navegar e fechar; reabrir e
   escolher Xbox Home. Confirmar a dashboard na mesma janela, sem repetir intro.
6. Fechar a janela ao concluir; guardar todos os logs, handoffs e eventual pilha.

Tela preta, processo vivo e saída zero não aprovam renderização ou execução XNA.
Se o título retorna sozinho, Guide/entrada/retorno solicitado pelo usuário ficam
pendentes. Nenhuma campanha ou ação de save/load faz parte deste teste curto.

## Regressões técnicas desta etapa

Commits locais: `4cab70e` (diagnóstico/armazenamento da sessão), `2385cf3`
(tipo de conteúdo, reconciliação de registros/cache) e `6946e3d` (arquivamento
da cópia obsoleta que ainda era enumerada sem header). Sem push.

Build focado `x360n_tests`/supervisor via `scripts/devshell.cmd`, sem relink de
títulos. Dois casos novos passaram **37 asserções**: exceção não tratada na
dashboard e no filho, captura somente quando solicitada, ausência de relaunch,
raízes de armazenamento/identidade ao atravessar os handoffs e recusa de destinos
de conteúdo/build/fontes, relativos ou ausentes. A captura existente em `run`
passou **14 asserções** antes da adição de armazenamento (sem alteração posterior
nessa rota). Processos sintéticos, sem XEX original ou janela de jogo.

O build final do frontend/supervisor e do teste desktop no preset Debug passou.
Sete casos de catálogo/biblioteca/AOT passaram inicialmente 156 asserções;
o fechamento após arquivar também a cópia obsoleta passou **165 asserções**,
incluindo recusa de arquivos adicionais desconhecidos. O teste opt-in com a cópia real passou **24
asserções**, verificando os recibos de Charlie/17559, instalação e roteamento.
Dois caches derivados foram arquivados com SHA-256 preservado; os **2.019
arquivos originais** monitorados continuaram idênticos. Os **983 arquivos** da
cópia obsoleta arquivada também conservaram os SHA-256 de sua proveniência.
Ambos os recibos mantiveram os hashes da tabela inicial. O ensaio real só escreveu
na cópia de evidências, sem reparar ou regravar a biblioteca original.

Falha inicial preservada: o fixture esperava a cópia em `.local/isolated`, mas
criava `isolated` na raiz temporária; corrigido o caminho do fixture. Repetição
focada aprovada. Warnings de agregados `ProcessSpec::should_cancel` permanecem
no build, sem tratá-los como falha funcional. Não foi executada suíte completa.

Outras falhas de desenvolvimento preservadas nos logs privados: nome incorreto
do helper de geração no primeiro build do fixture (corrigido para
`GenerateAotProject`); build recusado com saída 73 enquanto a sessão manual
possuía a trava (nenhum artefato alterado); três verificações iniciais do cache
falharam porque caminhos estendidos Windows não normalizam `/` (corrigido com
`make_preferred`, seguido do build e das 156 asserções aprovadas). As tentativas
de launcher sem confirmação de início foram interrompidas; não foram usadas
como evidência funcional. O acesso inicial ao índice Git foi recusado pelo
sandbox; os commits locais autorizados foram realizados pelo caminho permitido.

## Matriz de aceite atual

| Etapa | Evidência | Estado |
|---|---|---|
| Preparação e DLLs | Recibos e registro nativo existentes, revalidação atual | Aprovada tecnicamente |
| Lista My Games corrigida | Mantenedor confirmou um Charlie e nenhum item com hash; fechamento manual em 24,792 s | Aceite manual restrito da biblioteca |
| Pedido My Games → Charlie | Handoff original e EXE autenticado em `507d571` | Comprovado no log |
| Bootstrap HostLoader/RuntimeHost/NetCF | Registros reais e retorno após 1,431 s | Parcial; falha de inicialização |
| Execução XNA gerenciada | Sem marcador conclusivo de inicialização | Pendente |
| Renderização do jogo | Relato de tela preta | Não aprovada |
| Entrada/navegação no jogo | Nenhuma resposta demonstrada | Pendente |
| Guide → Xbox Home | Só retorno automático do bootstrap demonstrado | Pendente |
| Item chamado completo | Pedido `00004000` sem perfil certificado; nenhum processo Charlie iniciado | Falha de catálogo corrigida genericamente |
| Storage → perfis após retorno | Pilha da dashboard `sub_921AA480`, acesso guest `0x20` | Falha capturada; causa/correção pendentes |
| Gameplay/save/load | Fora do observado | Não aprovados |

A primeira falha de registro foi corrigida; a inicialização CLR/XNA continua
bloqueada antes do jogo. A instrumentação e o arquivamento de registros/caches
não são apresentados como implementação do runtime gerenciado nem gameplay.
