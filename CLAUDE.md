# CLAUDE.md — contexto persistente do x360-native

Leia também `STATUS.md` (estado real) e `docs/architecture.md` (decisões D1–D7).
`docs/CHECKLIST_FUNCIONAL_x360-native.md` é o checklist oficial de cobertura; atualizar
somente itens demonstrados, respeitando o alvo e o limite de cada execução.

## Objetivo

**Performance-first, correctness-preserving** é política permanente. Performance
é objetivo central, preservando execução correta e compatível dos originais.
Evitar overhead desnecessário no runtime; instrumentação opcional e desligada
normalmente. Toda otimização exige medição antes/depois em condições equivalentes,
sem reduzir fidelidade, ocultar erros, retirar sincronização necessária ou alterar
contratos PPC/XAM. Melhorias compartilhadas devem servir genericamente aos jogos,
sem hacks específicos. Avaliar continuamente compilador/AOT, CPU, memória, GPU e
carregamento pelos gargalos demonstrados. Preservar C7/C8, isolamento dos originais,
SDK pinado e patches reproduzíveis. Metodologia e roadmap: `docs/performance.md`.

Plataforma sobre ReXGlue, com Windows x86-64 como primeiro alvo funcional relatado
e Windows, Linux e macOS como objetivos, com estados separados por sistema e
arquitetura (x86-64 e ARM64 quando viáveis e implementados): jogos de Xbox 360 recompilados AOT para
código nativo e a **dashboard original** como alvo próprio (lançar jogo, salvar,
encerrar, voltar ao menu). "Todos os jogos" é meta de longo prazo, nunca uma
afirmação.

Todas as versões originais da dashboard, inclusive antigas e betas do acervo,
são objetivo permanente. 17559 é referência inicial; módulos, fontes e hipóteses
ficam associados ao perfil correspondente. Versões não suportadas permanecem
pendentes, sem herdar silenciosamente componentes da referência.

A distribuição deve oferecer um aplicativo pronto nas Releases, com um download
principal por sistema/arquitetura suportados e importação/preparação guiadas,
sem exigir ambiente de desenvolvimento configurado pelo usuário. Um build ou
preset não comprova suporte funcional. O assistente não substitui a dashboard,
My Games, Guide ou perfis originais. Preservar o núcleo compartilhado, isolar
diferenças de sistema e manter os resultados existentes; isso não autoriza troca
imediata de backend/SDK ou ports paralelos. Requisitos e aceite do pacote real:
`docs/CHECKLIST_FUNCIONAL_x360-native.md` §§15–18, ainda pendentes. Não distribuir
conteúdo proprietário ou código derivado dos binários sem autorização aplicável.

## Isolamento de versões — regra arquitetural permanente

**Uma dashboard, seus módulos originais correspondentes. Sem mistura de versões.**
Retail 17559 usa apenas módulos/recursos da 17559, 13604 apenas da 13604,
6717 apenas da 6717; vale para toda versão futura. Inclui teclado vk.xex,
signin.xex, gamerprofile.xex, XUI, skins, fontes e demais recursos específicos.
Igualdade de nomes, ordinais ou exports não é evidência de compatibilidade.
O vk.xex 13604 é incompatível e não substitui o vk.xex Retail 17559 encontrado.
Registrar versão, procedência e SHA-256 no perfil. Recusar dependências
divergentes/ausentes antes do runtime, com mensagem identificando versão e
arquivo necessário; proibir fallback silencioso, caminhos compartilhados e
caches globais de módulos originais. Adicionar/preservar verificação automática.
ReXGlue e infraestrutura genérica podem continuar compartilhados. Mapeamento
e guardas atuais: `docs/dashboard.md` §0.32 e `titles/dash_*/preparation.toml`.

## Commits e primeira publicação — regra permanente

- Todos os novos commits próprios usam Conventional Commits:
  `tipo(escopo): descrição objetiva da mudança`. Escolher o tipo pelo conteúdo
  real: `feat`, `fix`, `refactor`, `perf`, `test`, `docs`, `build`, `ci` ou `chore`.
  Exemplo: `docs(checklist): add cross-platform usability requirements`.
- Não incluir referências a Claude, Codex, Astra ou outros assistentes como
  autores no título, corpo ou rodapés. Não incluir “Generated with...”,
  “Implemented by...” ou `Co-Authored-By` atribuído a assistente.
- Preservar créditos humanos, licenças, avisos de terceiros e referências
  técnicas necessárias ao upstream. Não apagar instruções de agentes nem
  falsificar autoria de terceiros.
- A revisão de mensagens anterior ao primeiro push foi concluída em 2026-10-12,
  conforme checklist §19.2. Não reabrir esse trabalho nem reescrever commits
  publicados. A primeira pre-release pública foi expressamente autorizada pelo
  mantenedor em 2026-10-13; preservar avisos e executar os gates de distribuição.
- Antes da reescrita, identificar o intervalo, guardar backup recuperável e
  verificável fora do conjunto a publicar e preservar separadamente alterações
  não commitadas e arquivos não rastreados necessários, sem limpeza destrutiva.
  Preservar commits úteis, conteúdo, autores humanos, datas e histórico upstream;
  não condensar tudo num “initial commit” para esconder o histórico.
- Validar todas as mensagens e snapshots correspondentes; guardar mapeamento
  local de hashes antigos/novos e atualizar referências internas com cuidado,
  sem mudar hashes de dependências externas por engano. Auditar todo o histórico
  a publicar para credenciais, dados pessoais, conteúdo proprietário e arquivos
  indevidos. Não publicar backups/referências que reincorporem histórico sem
  revisão; criar tags de release sobre o histórico final validado.
- Se houver commits já compartilhados, informar antes de alterar esse histórico;
  não executar force-push automaticamente. A preparação não autoriza push,
  publicação de release ou mudança de visibilidade do repositório.

## Trabalho dos agentes — regras permanentes

- Computer Use é proibido sem autorização explícita do usuário.
- Testes visuais e sessões longas de jogos somente quando indispensáveis ao
  incremento e previamente autorizados.
- Não repetir regressões já aprovadas sem uma mudança que realmente possa
  afetá-las. Priorizar testes unitários e focados, usando logs e evidências existentes.
- Não rodar suítes completas automaticamente. Os comandos abaixo são referências,
  não autorização para executá-los.
- Nenhum processo em segundo plano sem acompanhamento e limite de duração.
- Se uma ferramenta exigir interação não autorizada, interromper e informar
  o bloqueio imediatamente. Não esperar horas por aprovação.
- Não ampliar o escopo nem começar outra funcionalidade antes de concluir a atual.

## Comandos (PowerShell, raiz do repositório)

```powershell
powershell -ExecutionPolicy Bypass -File scripts\bootstrap.ps1   # submódulos, LLVM 22.1.8, symlinks libmspack
scripts\devshell.cmd cmake --preset win-amd64                    # configure (~7 min a frio)
scripts\devshell.cmd cmake --build --preset win-amd64-debug
scripts\devshell.cmd ctest --preset win-amd64-debug -j 16        # tudo (SDK + projeto)
scripts\devshell.cmd ctest --preset win-amd64-debug -L project   # só este repositório
.local\build\win-amd64\src\supervisor\Debug\x360n.exe doctor
```

- Sempre compilar via `scripts\devshell.cmd` (vcvars64 + LLVM fixado). Não há
  clang no PATH global, de propósito.
- `devshell.cmd` repassa os argumentos com as regras do cmd: `|` vira pipe e `^`
  some. Filtre por nome sem âncora (`-R "local[.]"`, `-R "x360n[.]ppc"`) ou pelo
  label único `project`.
- A mensagem `'vswhere.exe' não é reconhecido` vem de `VsDevCmd.bat:181` quando
  `NoDefaultCurrentDirectoryInExePath=1`; o próprio script diz que não afeta o ambiente.
- Se o build falhar, **não rode os testes** sobre binários antigos.

Resultado de referência (2026-10-12, com os patches locais 0001–0006): 1726 testes,
1721 ok, 4 skip, 1 falha conhecida (`unit.Depfile escapes characters make treats as syntax`,
bug do teste upstream; é falha, não sucesso). `local.inventory …` lista XEX
antigos como não suportados sem falhar; falha num XEX2 malformado.
`local.integration targets …` falha se um alvo de `tests/local/integration_targets.tsv`
presente não for exatamente o arquivo listado. Qualquer outra falha é regressão.

## Estrutura

Estado funcional consolidado em [STATUS.md](STATUS.md). Evidências por versão
em [docs/dashboard.md](docs/dashboard.md), especialmente C7.7/C7.8 (§§0.31–0.32),
e saves em [docs/save-storage.md](docs/save-storage.md). A região experimental
17559 `0x8E030000` (64 KiB RW) permanece hipótese explícita, exclusiva do título.
Xenos é padrão; Native está suspenso, experimental e sem aceite de performance.
O lock contém 77 patches ativos, até 0078 (0073 retirado); não confundir a
numeração com a quantidade. Resultados históricos de testes ficam nos relatórios,
sem repetir suítes ou sessões visuais por uma mudança documental.

- `external/rexglue-sdk` — submódulo fixado em `deps.lock.json`. **Não editar à mão.**
  Exceções documentadas: cópias dos symlinks do libmspack (`scripts/materialize-symlinks.ps1`)
  e os patches locais de `patches/rexglue-sdk/` (0001–0002 autorizados em 2026-10-11,
  demais patches documentados individualmente no lock e nos relatórios), aplicados por
  `scripts/apply-sdk-patches.ps1` e verificados pelo CMake no configure e em todo build.
  `src/ui/rex_app.cpp` (patch 0004) é compilado em cada executável: recompile o título.
- Correções do SDK vão no `CMakeLists.txt` raiz como backport condicionado ao
  commit fixado, citando o commit upstream equivalente. Sem equivalente upstream,
  só como patch em `patches/rexglue-sdk/` com teste `sdkcheck.*` que falha sem ele
  e entrada em `deps.lock.json` (hashes antes/depois).
- Executáveis que ligam o runtime usam `x360n_stage_runtime_dlls()`: o SDK só copia
  a DLL ao relinkar o executável, e uma DLL velha ao lado do `.exe` passa despercebida.
  A cópia roda antes do link, também num build com `--target`.
- O carimbo do codegen não inclui o `rexglue`: depois de mudar o codegen, regenere com
  `rexglue codegen --ignore-stamp` (não `--force`).
- `src/supervisor` — `x360n` (doctor/identify/run/session). Não liga o runtime.
- `tests/` — prefixos: `x360n.*`, `x360n.ppc.*`, `sdkcheck.*`, `env.*`, `local.*`.
  Um único label CTest (`project`): listas de labels quebram `catch_discover_tests`.
- `titles/` — configuração por título/versão, indexada por SHA-256 do XEX.
- `.local/` — ignorado: toolchains, build, inputs (conteúdo do usuário), runs, logs.

## Regras

1. Não inventar: nomes de API, flags, targets, imports, endereços, hashes ou
   resultados. Confirmar no código da revisão fixada (`external/rexglue-sdk`);
   a wiki está desatualizada (ex.: `rexglue init` usa `--project-name/--xex-path`).
2. Marcar o que é fato, hipótese e não verificado. "Compilou" ≠ "inicia" ≠ "jogável".
   A matriz em `docs/compatibility.md` só avança com evidência (teste, hash, log).
3. Não esconder função ausente retornando sucesso/zero. Lembre: `REX_STUB` não
   define r3. Implementação parcial exige comportamento documentado e teste.
4. Não remover verificações nem afrouxar testes. Não usar `--force` no codegen sem
   registrar e sem marcar o resultado como não validado.
5. Para corrigir: usar evidências existentes ou reprodução focada → hipótese →
   mudança pequena → teste relevante, respeitando as regras de trabalho dos
   agentes. Não executar suíte completa automaticamente.
6. Caminho principal AOT. Sem Xenia como executor, sem JIT/interpretador, sem
   autorização explícita. Xenia é referência.
7. Um processo de teste que abre e fecha valida o supervisor, nunca a dashboard.
   A interface de terminal não é a dashboard.
8. Conteúdo proprietário (XEX, dashboard, saves) só em `.local/inputs`, fornecido
   pelo usuário. Não baixar SDKs vazados nem conteúdo de terceiros. `x360n run`
   recusa executáveis em `.local/inputs`; não executar scripts vindos de dados de jogos.
9. Correções específicas de título ficam em `titles/`; correção geral precisa de
   teste que não dependa só daquele jogo.
10. Sem `install` do SDK, instalação global ou admin sem explicar e pedir
    (o `install` escreve em `HKCU\Software\Kitware\CMake\Packages`).
11. Fora do escopo atual: Xbox Live, multiplayer online, periféricos especiais.
12. Não afirmar desempenho sem medir e descrever a medição.
