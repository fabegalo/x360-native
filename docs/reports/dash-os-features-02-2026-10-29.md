# DASH-OS-FEATURES-02

Incremento em andamento sobre `main` inicial `d17229f`, com trabalho em
`codex/dash-os-features-02`. Mudanças paralelas do launcher/licenças preservadas.
O pedido completo ainda não está concluído. Sem push ou Release.

## Implementação

- 17559: os exports `XamReadImage`, `XamInitializeGameTileCache`,
  `XamReadGameTileImage` e `XamDestroyGameTileCache` executam o cache original.
  O host carrega as tabelas originais depois da skin, na ordem de
  `816CEA68..816CEA90`, antes do construtor `81691ED0`. Nenhuma imagem criada
  pelo host; PNG, texturas, polling e descarte continuam no PowerPC original.
- 0217: `XamEnumerate(flags=2)` aceita o despacho direto de títulos/conquistas,
  inclusive OVERLAPPED, sem consumir páginas diante de buffers inválidos.
- 0218: falha na limpeza da origem de uma movimentação preserva o destino
  completo já confirmado e devolve o erro real. A origem pode ter sido
  parcialmente removida; não há promessa de rollback nem sucesso nessa condição.
- 0219: a classe virtual `0x20000000` de `XamAllocEx` usa armazenamento guest
  real, com tamanho/liberação normais; `XamAlloc` encaminha ao mesmo contrato.
  Confirmada em `816E67E8 → 816E5F10 → 816E5E40` (pool virtual 3), sem tratar
  memória física como virtual. Quotas dos pools originais continuam ausentes.

As implementações compartilháveis ficam no SDK pinado, por patches e hashes no
lock. Endereços e inicialização da 17559 ficam somente no seu perfil.

## Verificação focada

| Caso | Antes | Depois |
|---|---|---|
| Enumeração direta | 2 falhas / 27 asserções | Incluído no integrado aprovado |
| Falha ao apagar header da origem | Destino completo também removido; 1 falha / 9 | Bytes e catálogo do destino sobrevivem ao restart |
| Classe virtual do cache | `80004001`; 1 falha / 27 | Alocação, tamanho e liberação reais |
| Runtime integrado | — | 2.170 asserções / 38 casos aprovados |
| Conquistas em processos independentes | — | Escrita/leitura de dois perfis e dois jogos aprovadas |

O teste de movimentação usa exclusivamente volumes temporários e um handle
Windows que permite ler o header, mas recusa sua exclusão. Exercita a API PPC,
o backend real e uma segunda instância do runtime. Não destrói dados do usuário.

Uma rodada anterior no sandbox teve uma falha de I/O na troca de preferências;
os dois casos correspondentes passaram isoladamente fora dele. Os resultados
iniciais permanecem nos logs; não são apresentados como uma rodada toda verde.

Evidências técnicas em
`.local/worktrees/dash-os-features-02/.local/evidence/dash-os-features-02/`.

## Interface original

Computer Use e sessões gráficas limitadas foram autorizados explicitamente.
Seed e working ficam em `.local/diagnostics/dash-os-features-02-20261029/`.
Os 18 arquivos copiados da origem mantiveram seus SHA-256 após as duas primeiras
sessões. Nenhum desbloqueio foi fabricado para homologação visual.

- `20261030T001534Z-session-0-dash_17559`: boot original; saída normal em
  144.521 ms. Não certificou navegação do Guide.
- `20261030T002006Z-dash-os-images`: reproduziu a falha real do Guide:
  construtor do cache recusado por `XamAllocEx(0x20000000)`; resultado da UI
  `80004001`/1627. Saída normal em 157.663 ms não é PASS do Guide.
- A tentativa seguinte com SDK local foi recusada por objetos antigos sem
  registros de dependência. Não foi executado o produto incompleto.
- `20261030T004546Z-dash-os-images-0219`: preparo padrão concluído, recursos
  originais carregados e scratch virtual alocado. Guide ainda falhou com
  `8007000E`/14: o cabeçalho de textura usava o heap privado não iniciado,
  através de `816E6C78`. Saída normal em 269.829 ms não certifica o Guide.
- Correção seguinte: ponte do alocador virtual com tag `816E6C78(size, tag)`
  para memória guest alinhada em 32 bytes, com liberação pelas pontes já
  existentes de XMemFree/delete. Nenhuma substituição da textura original.
  Preparo oficial concluído; no reteste o cache e a abertura do Guide retornaram
  zero. A primeira interação automática foi interrompida pelo Escape físico.
- `20261030T010131Z-dash-os-user-review`: sessão manual pedida pelo mantenedor,
  encerrada normalmente em 136.221 ms. Logs demonstram Guide, troca para o
  perfil e restauração da pilha em vários ciclos. O mantenedor observou corte
  no caminho direto Guide → Conquistas, ausente em Perfil → Ver jogos →
  Conquistas. Não é aceite de imagens ou fidelidade integral.
- Comparação com `816CE108`: a troca para o sucessor/pai ocorre antes da
  ocultação final (`816CE22C..816CE348` versus `816CE390..816CE478`). O host
  ocultava o quadro e emitia a notificação de saída a cada troca de módulo.
  Corrigido para conservar o quadro, dimming e estado visível até o fim da
  pilha, carregando o sucessor na mesma passagem. Sem animação criada pelo
  host. Testes focados: 23 casos / 1.004 asserções aprovados.
- `20261030T011940Z-dash-os-handoff-original`: preparo oficial aprovado;
  Guide → Conquistas → Forza Horizon → grade → detalhes → voltar até o Guide
  observado por Computer Use. Ícone do jogo, imagem original da conquista
  existente, texto em português, 10 G e 1/50 desbloqueadas visíveis. Guide
  restaurou sua aba/seleção. Nenhum desbloqueio criado. O fundo permaneceu
  escurecido nas etapas observadas, sem retornar à dashboard entre módulos.
  Capturas pontuais não certificam igualdade quadro a quadro com o console.
  Setas/Shift+setas injetadas não mudaram a seleção da grade; A/B e ombro
  funcionaram. Controle físico e seleção das demais conquistas não certificados.
  Janela deixada para inspeção manual, sob supervisor com limite de 900 s.
  Após testar a correção, o mantenedor confirmou: “bem melhor agora”.

## Limites do incremento

Ainda sem novo aceite: Gamerpic pessoal persistente, Guide/perfis em múltiplos
ciclos completos, todas as conquistas da grade, notificação por evento
real, operações de Storage pela UI, FE/2000C, cancelamento por
`XMsgCancelIORequest`, configurações, My Games/retorno e regressão visual 1888.
6717 continua exigindo seus próprios recursos/fontes; nenhum recurso de outra
versão foi usado. Os testes sintéticos acima não homologam esses fluxos.
