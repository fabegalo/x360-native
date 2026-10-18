# Migração do ambiente local para Release — 18/10/2026

Escopo: migrar os produtos nativos ativos do launcher local para Release e
atualizar sua inicialização, sem push/publicação, alterações no SDK ou nos
originais, perfis, saves e renderer. O conteúdo PowerPC importado não possui
modo Debug/Release; a migração trata dos executáveis/DLLs host preparados.

**Escopo final reduzido pelo mantenedor:** concluir somente a 17559 e executar
Castle Crashers diretamente, com smokes limitados, para encerrar esta rodada.
As recertificações restantes e os benchmarks dos seis títulos foram cancelados.
Os resultados iniciais abaixo registram o trabalho já realizado; não certificam
que os outros títulos estão prontos para lançamento após a alteração de metadados.

## Inicialização e frontend

O caminho usado pelo atalho era `.local/build/desktop-only/src/desktop/x360-native.exe`,
compilado em Debug. Essa mesma árvore foi reconfigurada e reconstruída em Release,
incluindo supervisor e extrator. A cópia de `x360n.exe` junto ao launcher foi
conferida contra o produtor em `src/supervisor`.

`scripts/start-desktop.cmd` agora exige cache Release para `desktop-only`, e seu
único fallback é o diretório `Release` do preset multi-config. Não há fallback
Debug. A opção diagnóstica `--print-path` valida a seleção sem abrir aplicativo
ou jogo. Testes com caminhos contendo espaços verificam rejeição de Debug,
seleção do Release local e fallback exclusivamente Release, em PowerShell 5.1/7.

O atalho `x360-native (main).lnk` da Área de Trabalho passou a usar esse script,
mantendo o ícone do aplicativo e o ambiente raiz. Seu arquivo e propriedades
anteriores foram preservados na evidência privada.

## Produtos existentes e resultado da migração

Os sete alvos nativos existentes no catálogo são 17559, 6717, Charlie Murder,
Motocross Madness, Castle Crashers, Forza Horizon e Minecraft Xbox 360 Edition.
Os dois projetos AOT genéricos são regenerados pelo gerador oficial antes da
preparação. Builds legados/testes e snapshots históricos não são selecionados
pelo launcher; não são apresentados como produtos de produção.

Cada título passa pelo driver oficial: configure Release, ferramenta SDK,
codegen, reconfigure, build/link, conferência dos comandos de objetos/PCH e
publicação do recibo somente depois dos gates. O runner tem Job Object,
acompanhamento e limite de 40 minutos por preparação. Builds são seriais,
com até oito tarefas Ninja, respeitando os locks existentes.

Seis produtos foram reconstruídos e certificados em Release: 17559, Charlie,
Motocross, Castle, Forza Horizon e Minecraft. A 6717 foi bloqueada no primeiro
configure: `XenonJKLatin.xtt has no verified matching version/hash; no fallback`.
Falta o original correspondente e sua identidade verificada. O gate foi
preservado; a migração desse alvo não foi concluída. Seu executável/recibo
anteriores foram arquivados intactos e o marcador de preparação incompleta
permanece. Ela não oferece fallback RelWithDebInfo nem sessão guiada.
Títulos apenas importados, sem produto nativo preparado, não são declarados
compatíveis por esta migração.

## Falha encontrada e correção

A primeira preparação 17559 compilou/linkou em Release, mas o gate final
recusou publicar o recibo com `Release compiler evidence is empty`. O driver
usa Windows PowerShell 5.1; nesse host, `@(... | ConvertFrom-Json)` pode conter
um array aninhado, diferente do comportamento exercitado anteriormente em 7.

Corrigida a normalização no gate e no consumidor de comandos do pacote. Há teste
de JSON decodificado que aprova Release e recusa objeto Debug nos dois hosts.
Correção registrada em `b4ac434`. A preparação foi retomada pelo caminho oficial,
sem apagar manualmente o marcador ou assinar os produtos de uma tentativa falha.
Os logs da falha permanecem preservados.

A segunda tentativa passou pelo gate de flags e foi recusada pelo inventário:
as DLLs `rexruntimerd`, `rexgpu-xenosrd`, `rexgpu-nativerd` e `TracyClientrd`
da configuração anterior ainda estavam na pasta de staging. Foram arquivadas
25 cópias derivadas, somente nas sete árvores ativas, sob lease exclusivo e
com SHA-256 antes/depois. Nenhum original foi movido. A preparação foi retomada
normalmente; o gate de inventário não foi relaxado.

O snapshot completo do catálogo encontrou uma terceira incompatibilidade de
integração: os quatro `preparation.toml` de 17559/Moto/Castle/Forza ainda
exigiam `rexruntimerd.dll`, `rexgpu-xenosrd.dll` e `TracyClientrd.dll`. Os
recibos dos binários estavam íntegros, mas o preflight recusou o lançamento.
As listas passaram a exigir `rexruntime.dll`/`rexgpu-xenos.dll`, preservando
os módulos particulares de cada título; Tracy não é produzido/staged nessa
configuração Release. Correção e regressão nos hosts PowerShell 5.1/7 em
`5441e7f`. Os quatro títulos foram enviados novamente ao driver para
recertificar o metadado autenticado. A 17559 exigiu reconstrução completa
porque o checkpoint foi recusado; não foi forçado reuso.

## Validação

A auditoria dos seis produtos certificados confere caches Release, comandos expandidos do Ninja,
SHA-256 dos inputs/artefatos dos recibos, inventários e imports PE que poderiam
indicar CRT/runtime Debug. O snapshot de preservação cobre 1.161 arquivos de
usuário. A seleção do startup/atalho e as validações de lançamento são verificadas
separadamente da duração do build e do primeiro frame.

Auditoria inicial independente aprovada nos seis produtos, antes da correção
dos metadados de lançamento: 64 executáveis/DLLs e
5.811 comandos C/C++/PCH nos grafos completos (incluindo ferramenta, SDK e
runtime). Todos os comandos selecionados têm `-O3 -DNDEBUG`; nenhum import
PE de CRT/runtime Debug foi detectado. Os hashes dos supervisores/extratores
copiados ao lado do launcher coincidem com os respectivos produtores Release.
Os 1.161 arquivos originais de usuário foram preservados.

| Produto | Preparação oficial (s) | Inputs do recibo | Artefatos | Comandos C/C++/PCH |
|---|---:|---:|---:|---:|
| 17559 | 538,34 | 3.960 | 12 | 1.033 |
| Charlie Murder | 404,48 | 3.994 | 34 | 1.000 |
| Motocross Madness | 610,79 | 4.114 | 5 | 1.123 |
| Castle Crashers | 313,25 | 3.464 | 4 | 799 |
| Forza Horizon | 666,53 | 4.050 | 6 | 1.087 |
| Minecraft | 276,63 | 3.438 | 3 | 769 |

Tempos de preparação são os das primeiras compilações Release bem-sucedidas;
não são latência de lançamento nem incluem a recertificação dos metadados.
A 6717 falhou em 48,70 s na configuração, sem produzir
um novo executável. As duas tentativas anteriores da 17559 recusadas pelos
gates também permanecem registradas, sem serem contadas como sucesso.

Após a redução do escopo, a recertificação final foi concluída somente para
17559 (574,22 s) e Castle (339,72 s), novamente pelo driver oficial. O checkpoint
de Castle foi explicitamente recusado por diferença na política; ambos fizeram
reconstrução completa. A recertificação de Motocross foi cancelada, com o Job
Object encerrando a árvore de processos e o marcador incompleto preservado;
Forza não foi recertificado. Seus executáveis já compilados são Release, mas
eles precisam de Preparar para voltar a satisfazer os metadados autenticados.
Charlie/Minecraft mantêm os recibos Release anteriores válidos. Não foi
executado nenhum desses quatro títulos nesta rodada final.

Os benchmarks completos foram retirados do escopo final. O primeiro preflight
registrado havia sido recusado pelas listas `rd`; esse resultado não é tratado
como medição de desempenho. Não se apresenta novo ganho comparativo de startup:
além da configuração, o conjunto de títulos preparados mudou. As comparações
alternadas históricas continuam no relatório de preflight correspondente.

## Aceite final reduzido: 17559 e Castle executados

Ambos passaram novamente pela validação de lançamento e executaram em Release
por até 30 s, com dados de usuário copiados para a evidência isolada. Foram
encerrados pelo Job Object ao atingir o prazo; `exit_code=1` nesse caso indica
o encerramento intencional, não falha de inicialização. Nenhum processo desses
smokes/builds permaneceu ativo ao concluir a coleta.

| Alvo | Preflight (s) | Processo iniciado (s desde o helper) | Primeiro swap (s do runtime) | Swaps | Falhas de swap |
|---|---:|---:|---:|---:|---:|
| Castle Crashers | 5,498 | 5,606 | 0,502 | 1.620 | 0 |
| Dashboard 17559 | 13,354 | 13,862 | 0,629 | 1.620 | 0 |

Castle autenticou 5.687 arquivos/2.083.843.744 bytes; 17559 autenticou
13.206 arquivos/18.215.081.574 bytes. Estes são bytes SHA-256 da operação,
não tráfego físico de disco. Não houve coleta final de I/O por etapa nem
benchmark alternado após a redução de escopo. O relógio do runtime não começa
no clique do launcher; os tempos da tabela não certificam clique até imagem.

A auditoria final dos dois alvos aprovou os recibos, inventários, 16 artefatos
e 1.832 comandos C/C++/PCH Release, sem imports de runtime/CRT Debug. Foram
novamente preservados os 1.161 arquivos preexistentes de usuário auditados.
O log de Castle mantém avisos de busca de `SobelEdge.xpu` e
`VertexColorFillPS.xpu`; não foram alterados originais nem acrescentados stubs.
Swaps demonstram atividade de apresentação, sem certificar navegação, gameplay,
save/load ou aceite visual. Nenhum outro jogo foi executado nesta rodada final.

Inicialização e teste correspondente registrados em `316d4d6`; nomes Release
dos metadados em `5441e7f`; compatibilidade do gate com PowerShell 5.1 em
`b4ac434`. Todos são commits locais, sem push/publicação.

Evidências privadas em `.local/evidence/local-release-migration-2026-10-18/`:
logs/JSONs de cada preparação, identidades antes/depois, auditorias de flags e
imports, snapshot dos dados de usuário e backup/propriedades do atalho.
