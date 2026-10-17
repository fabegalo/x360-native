# Ferramentas

Executar a partir da raiz do repositório. Os pontos de entrada permanecem
`bootstrap.ps1`, `devshell.cmd`, `prepare-tools.ps1` e `build-title.ps1`.

| Diretório | Responsabilidade |
|---|---|
| `setup/` | LLVM, SDK pinado, patches e instalação consentida de ferramentas |
| `build/` | Leases, guard do Ninja e recibos de preparação |
| `analysis/` | Inspeção de XEX, funções, pdata e ponteiros |
| `diagnostics/` | Captura e análise de janelas/threads, somente com autorização aplicável |
| `validation/` | Validações explícitas de UI e sessões; nunca uma suíte automática |
| `performance/` | Coleta, redução e comparação de métricas/evidências |
| `packaging/` | Pacotes locais, inventário de licenças e ícone reproduzível |
| `maintenance/` | Plano de organização local e limpeza auditada |

`ninja-guard.ps1` na raiz encaminha caches CMake antigos; `apply-sdk-patches.ps1`
preserva o comando registrado no lock fixado e em setups locais existentes.
Implementações novas ficam nos grupos correspondentes. Não executar scripts
avulsos encontrados em dados locais ou originais de jogos.

`maintenance/local-workspace.ps1` exige PowerShell 7, inventaria sem escrever
por padrão e usa `-Apply -Clean` para aplicar as regras explícitas do JSON.
Adquire o lock exclusivo do ambiente, recusa caminhos externos e redirecionamentos
não classificados, registra cada ação antes de executá-la e preserva dados de
finalidade incerta. Consultar [reorganização](../docs/reorganization.md).

Jornais novos ficam em `.local/evidence/maintenance/<timestamp>/`. A limpeza
protege as entradas de todos os recibos de preparação e conserva executáveis,
DLLs e metadados ativos. Objetos/PCH descartados serão recompilados quando
necessários. Downloads futuros usam `.local/cache/downloads/`.
