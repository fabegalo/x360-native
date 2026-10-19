# Correções da revisão de entrada, distribuição e atualização

Base desta tarefa: `477164b8c468714d6fb07e8011aba94776c77fa4`, na `main` local.
O commit posterior à referência auditada modifica a biblioteca; não corrige os
achados aqui tratados. A árvore principal estava limpa, com os patches esperados
no submódulo. Não houve execução de dashboards/jogos nem Computer Use.

## Entrada — correção e evidência nova

| Achado | Reprodução | Correção | Teste e resultado | Limite |
|---|---|---|---|---|
| DOWN pendente após desconexão | Teste integrado no backend real do SDK retornou sucesso em vez de 1167 | Patch 0102 descarta estado na perda/troca da conexão | Release: reprodução com 3 falhas; regressão final aprovada | Controle físico ainda pendente |
| Pendência sobrevive a shutdown/backend e outro runtime | Dois testes sem drenar artificialmente KEYUP reproduziram o vazamento global | Estado pertence à instância de entrada; Shutdown, atribuição e identidade invalidam pendências e posse | `[guide-lifecycle]` integrado | Morte de um processo encerra sua memória; não há estado de entrada persistido |
| GetState contorna o atraso | Primeiro BACK visível enquanto o evento aguardava | Decisão compartilhada de janela entre estado/eventos | Quatro índices, atalho, repetições, soltura e desativação com pendência | Janela é heurística opt-in |
| ANY_USER consultava apenas índice 0 | Backend sintético com eventos em cada índice | Busca dos quatro índices, respeitando o dono da aplicação de sistema | ANY_USER do título/HUD Ex aprovado | Estado/capacidades preservam sua ABI existente |
| Atraso automático em todas as dashboards | Defaults de 150 ms nos dois hosts | Removidos defaults; `guide_chord_ms=0` permanece padrão | Índice não selecionado e Guide dedicado imediatos | Não há calibração física dos 150 ms |

Configuração explícita para um controle que emula Guide por BACK+START:
`--guide_chord_ms=150 --guide_chord_users=1`. A máscara seleciona índices guest:
1, 2, 4, 8; 15 seleciona todos. O alcance é o usuário selecionado e seus
consumidores de entrada XAM, inclusive HUD, para que um consumidor não contorne
o outro. Dispositivos mesclados no mesmo usuário compartilham essa política;
não habilitar a janela em usuários que não precisam do atalho. Limite aceito:
0–1000 ms. Sem sleeps no runtime. Desativar libera a pendência na ordem original.

Execução nova Release: **500 asserções / 18 casos** com filtros
`[guide-lifecycle],[guide-chord],[guide-opening],[guide-input],[profile-ui]`.
Logs: `.local/evidence/review-fixes-20261019/input-red.txt` e `input-final.txt`.
A primeira tentativa verde detectou um acesso nulo em runtime sem backend;
corrigido e repetido com sucesso. O primeiro wrapper do build ficou sem progresso
após link; encerrado e repetido com limite explícito. Nenhum teste sobre build
fracassado foi contado. Não há alegação de ganho de performance.

## Pendências desta tarefa

Distribuição/0095, transação do atualizador, recertificação Release dos produtos,
pacote candidato e aceite manual ainda em andamento. Evidências históricas do
relatório de Guide não são recertificação desta tarefa. Encerramento organizado
continua distinto de reboot fiel do console.
