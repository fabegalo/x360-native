# STABLE-02 — ensaio futuro de persistência real

**NOT RUN.** Preparação de dados não autoriza gameplay/Computer Use nem aprova
save/load. Alvo: main `f84436df94ab2e17275fa4272107cc1aa4c400f3`, Windows x64,
Release, Xenos D3D12, Retail 17559. Qualquer execução futura deve registrar seu
próprio SHA, lock, recibos e hashes; não herda os resultados desta revisão.

## Título recomendado e condições

**Forza Horizon 1**, Title ID `4D5309C9`, versão/base `0.0.0.10`, mídia
`2DC7007B`, XEX SHA-256
`db40df605ade49a612b35a7a24c38f6004bcb17a88ed6b48288de16df9e3987c`.
É o melhor candidato disponível pela [evidência anterior](../reports/titles/validation-forza-horizon.md):
ciclo original completo, condução, retorno, perfil retido e alteração do próprio
payload `ForzaProfile`. Isso não demonstrou carregar esse progresso após reinício.
Seu recibo é antigo e precisa de preparo oficial no lock atual antes de executar.

Usar o disco/conteúdo local fornecido e autorizado pelo mantenedor, em modo
integral que permita salvar, com direito de uso confirmado para esse conteúdo.
A identificação de disco extraído não comprova licença nem autoriza distribuição.
Não mudar máscara de licença, classificação de conteúdo, XUID ou bytes do jogo.
Se o próprio jogo indicar trial, licença ausente ou impossibilidade de salvar,
parar e registrar o bloqueio, sem fabricar saves ou liberar conteúdo.

Alternativas: Forza Horizon 2 (`4D530AA4`, `0.0.0.12`) tem recibo atual e
condução histórica em 0212, mas a auditoria 0214 ficou em “Pressione Start”.
Minecraft (`584111F7`, `0.0.0.1`) está no tutorial de avaliação; não comprova
save/load integral. Castle está incompleto e em trial. Nenhum é aprovado
antecipadamente nem evita a condição de licença real.

## Cópias preparadas, exclusivamente locais

`.local/evidence/stable-02/persistence-01/` contém `seed/` e `working/`, cópias
independentes sem links, e `snapshot.json` com inventário relativo, tamanhos e
SHA-256, origem, revisão e confirmação de igualdade após a cópia. O preparador
local `../prepare_snapshot.py` só copia e verifica; não inicia executáveis.

Cada cópia inclui **todo** `.local/user/dash17559` (HDD único, biblioteca
instalada, todos os perfis, saves, configurações, caches e arquivos ocultos),
mais `.local/user/forza_horizon` inteiro, para o cache específico do participante.
Não se copiou apenas `profiles/`. `seed` é referência a preservar;
somente `working` poderá receber gravações no ensaio. Não são hardlinks/junctions.
Outros consoles, originais em `.local/inputs` e recibos permanecem na origem.
Inventário privado não entra no Git; XERJ também permanece fora do Git.

O override da sessão recebe o **pai** de `dash17559`, não a pasta do HDD:
`--user-data-root <.../working>`. O supervisor acrescenta o basename de cada
perfil; `SessionPlatformArguments` mantém perfil e conteúdo dos jogos no HDD
da 17559. Não alterar `.local/user-data-root.txt` nem mover saves entre raízes.
As preferências globais do supervisor continuam em `.local`; inventariá-las
separadamente e não modificá-las durante o ensaio.

## Procedimento exato após autorização específica

1. Confirmar título, modo integral, controlador e limite total com o mantenedor.
   Propor até 10 minutos acompanhados por tentativa, no máximo duas tentativas;
   isso ainda requer autorização. Não estender a sessão se o próximo autosave
   não couber no limite. Encerrar e registrar NOT RUN/incompleto.
2. Sem sessão/preparo concorrente, renovar pelos drivers existentes os produtos
   necessários, sem `--force` ou edição de recibo:
   `powershell -NoProfile -ExecutionPolicy Bypass -File scripts/build-title.ps1 -Title forza_horizon`
   e, se o recibo da dashboard for recusado,
   `powershell -NoProfile -ExecutionPolicy Bypass -File scripts/build-title.ps1 -Title dash_17559`.
   Acompanhar cada build com limite explícito, parar se falhar e conservar logs.
   Exigir inspeção de produção aceita, ausência de marcador incompleto,
   inscrição My Games e todos os recursos originais 17559 identificados.
3. Revalidar `snapshot.json` contra origem, seed e working, inclusive arquivos
   adicionados/retirados. Se mudou desde STABLE-02, criar nova cópia completa em
   diretório novo e novo inventário. Registrar XUIDs reais apenas na evidência
   local, Title ID/mídia/versão, licença observada, SHA dos XEX/módulos/fontes,
   recibos, EXE/DLL, lock e preferências (`graphics.toml`, `console.toml`, seleção
   de storage). Preservar inventário dos originais dos recibos antes/depois.
4. Com Xenos/D3D12 selecionado no aplicativo, iniciar uma sessão supervisionada:

   ```powershell
   & .local/build/win-amd64/src/supervisor/Release/x360n.exe session titles/dash_17559/dash_17559_manifest.toml --user-data-root C:/Users/fabeg/Desktop/x360/.local/evidence/stable-02/persistence-01/working --timeout 600
   ```

   `--timeout` limita cada filho; acompanhar também o teto total da tentativa.
   Usar a UI original Home → perfil A → My Games → Forza. Não iniciar o EXE do
   jogo diretamente. Registrar foco/controle, áudio, perfil e HDD selecionado.
5. Carregar o progresso existente ou iniciar campanha pela opção original.
   Anotar um marco observável antes e depois (evento concluído, carro ou outro
   progresso que a UI efetivamente mostre). Atingir o primeiro salvamento legítimo;
   aguardar desaparecer o indicador original. Registrar o que aconteceu, sem
   presumir que conduzir por alguns segundos grava progresso. Se não houver
   save confirmado dentro do limite, o critério continua NOT RUN.
6. Abrir Guide → Xbox Home → confirmação original; verificar retorno à mesma
   17559, perfil A, controle e áudio sem duplicação/continuação do jogo encerrado.
   My Games → reabrir Forza, carregar e confirmar o marco exato. Guardar logs,
   handoffs, PIDs, caminhos de run e capturas autorizadas. Fechar a janela não
   substitui o retorno original e não deve ser contado como Xbox Home.
7. Retornar à dashboard normalmente e fechar a plataforma. Confirmar término
   de filhos; reiniciar o supervisor com a mesma `working`, carregar pela UI e
   confirmar novamente o marco. Não recriar working a partir de seed entre
   save e load. Registrar novas pastas de run e ausência de reutilização de
   handoffs/recursos fechados; repetir rapidamente somente dentro do limite.
8. Pela UI original alternar para perfil B e verificar que não herda o progresso
   de A; voltar a A e conferir o marco. Não criar progresso em outro título sem
   autorização. Comparar pacotes/saves dos demais perfis e títulos antes/depois,
   justificando separadamente metadados de console que a UI possa atualizar.
9. Após fechamento normal, inventariar working, seed e origem: seed e origem
   devem ser idênticos ao inventário inicial; diferenças de working devem ser
   explicadas pelo jogo/console. Hash diferente sozinho não prova save válido;
   exige load observado. Preservar working para análise, sem restaurar por cima
   dos originais. Timeout/crash antes de concluir não vale como fechamento normal.
10. Revisar evidências por critério, vinculadas à identidade exata. Não editar
    NOT RUN para PASS sem observação real. Save/load, ciclo original, importação
    pela UI e Windows limpo são quatro aceites distintos. Executar `verify`;
    quaisquer aceites restantes pendentes devem continuar recusando v1.0.0.

## Bloqueios concretos dos outros aceites

**Importação pela UI:** importação/preparação sintéticas passam separadamente;
nenhuma nova interação visual foi autorizada nesta tarefa. O Forza 1 está com
recibo antigo; o preparador CLI não demonstra selecionar origem, acompanhar
progresso/erro na UI ou recuperar preparo interrompido no aplicativo. Falta
observação desse fluxo com recibo novo e entrada My Games, incluindo interrupção
e recusa do produto incompleto. Não foi identificado um novo defeito da UI.

**Windows limpo:** este host contém LLVM, MSVC, Windows SDK, Python e árvores
de desenvolvimento; não serve como ambiente limpo. Não há máquina/VM limpa
designada nem pacote exato com inventário escolhido para este ensaio. Falta
executar primeira instalação, dependências ausentes, erros de preparo e
atualização/rollback preservando perfis/saves nesse ambiente. O teste sintético
de recuperação não satisfaz esse aceite. Não houve tag, Release ou publicação.
