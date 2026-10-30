# Pacote Linux x64 e gate de licenças/conteúdo — 2026-10-30

Primeiro pacote de distribuição Linux do launcher unificado, gerado por
`scripts/packaging/package-desktop.ps1 -Platform linux-x64` a partir do commit
`adb71e7`. Candidato local, **não publicado**.

## Como é gerado

- Mesmo script do Windows: exporta o commit, clona o SDK pinado
  (`f5337cdc947ff6d4c4196737e2c807a48f2a1fc2`), aplica e confere os patches do
  lock, gera o payload `app/workspace` com `payload.sha256`.
- Compilação em WSL por `scripts/packaging/build-desktop-linux.sh`: fonte e build
  irmãos no ext4, LLVM 22.1.8 Linux e sysroot privado (Ubuntu 26.04, glibc 2.43,
  GCC 15.2.0-16ubuntu1), `-march=x86-64-v2`, Release, `X360_DISTRIBUTION=ON`,
  `-static-libstdc++ -static-libgcc`. Sem pacotes globais nem `LD_LIBRARY_PATH`
  do usuário.
- Testes da árvore isolada: `desktop.catalog` e `desktop.update-recovery` PASS.
- Dependências ELF lidas com `llvm-readobj`: apenas `libc.so.6`, `libm.so.6`,
  `ld-linux-x86-64.so.2`; `libx360n-mspack.so` (LGPL, substituível) só no
  extrator. Nenhuma versão `GLIBCXX`/`CXXABI`/`GCC` exigida.
- `.tar.gz` determinístico (ordem por nome, mtime fixo, dono 0/0, `gzip -n`),
  modos 0755 nos 4 executáveis e 0644 no restante.

## Resultado

| Item | Evidência |
|---|---|
| Arquivo | `x360-native-1.12.1-linux-x64.tar.gz`, 13.657.904 bytes, SHA-256 `cf13e5fc8bedc389183a75da58911808f932de40b17061eb7bb58cc8ac63de11` |
| Gate de licenças/conteúdo | PASS: 96 componentes, 4 executáveis linux-x64, notices originais e identidade do payload |
| Piso de glibc medido | **2.43**: `acosf`/`asinf`/`atan2f@GLIBC_2.43` no launcher (libm do sysroot); `__isoc23_*@GLIBC_2.38` em todos |
| Uso como usuário (WSL Ubuntu 26.04, glibc 2.43, sem sysroot) | SHA-256 publicado confere; `package.sha256` confere; modos preservados; `ldd` sem ausentes; `app/x360n --help` exit 0; launcher abre com SDL_GPU Vulkan |
| Extrator empacotado, Castle Crashers XBLA real | `x360n-extract extract`: 708 arquivos, 143.818.078 bytes, 0 erros, 10 s; SHA-256 do original igual antes/depois |

Importação pelo launcher do pacote não foi automatizada: as opções de teste
(`--import`, `--auto-log`) são excluídas do build de distribuição por desenho.
A mesma importação do Castle Crashers pelo launcher de desenvolvimento passou
no Windows (38 s) e no Linux (17 s), com registro de catálogo
`5126950ab93e695bdfc3a2bf59aa42d12e08b62794bb9a024b3151711fbc2b51` nos dois.
Nenhuma ISO de Xbox 360 foi encontrada nos discos locais (só ISOs de PSP), por
isso o caminho ISO não foi exercitado com conteúdo real.

## Gate por plataforma

`licenses/inventory.json` agora separa `platforms.windows-x64` e
`platforms.linux-x64` (executáveis, unidades SDK/terceiros/geradas, bibliotecas
ligadas, entradas transitivas do SDK, componentes de runtime e dependências
vendorizadas). O escopo Windows é o mesmo de antes. O Linux acrescenta:

- unidades POSIX do SDK, com os cabeçalhos originais em `rexglue-attributions.txt`;
- fontes Linux/X11/Wayland do SDL3 e 25 protocolos Wayland gerados;
- `aes_128` (MIT) e TinySHA1 (ISC), compilados só no extrator Linux;
- `gcc-runtime` (GPL-3.0-or-later WITH GCC-exception-3.1, libstdc++/libgcc
  estáticos) e `glibc` (LGPL-2.1-or-later, do sistema, não distribuída), com os
  arquivos `copyright` originais do Ubuntu em `licenses/`.

Os depfiles de link do Linux listam objetos `.o`; eles não são lidos como
entradas do SDK.

Verificação do gate: candidato Windows 1.10.1 com o inventário novo,
`tests/packaging/licenses.ps1` com 1 positivo + 24 negativos PASS; Linux com 7
negativos focados (`.so` estranho, ELF renomeado, executável ausente, link
estático, header fora do escopo, notice de runtime ausente, plataforma
desconhecida), todos rejeitados com a mensagem esperada.

## Limites

- O piso glibc 2.43 restringe o pacote a distribuições muito recentes (o SDK
  upstream compila no jammy para manter 2.35). Reduzi-lo exige um sysroot mais
  antigo, ainda não obtido; nada foi baixado.
- O empacotamento Linux exige host Windows com WSL. Um runner Linux nativo
  (PowerShell 7 + o mesmo script de build) ainda não foi feito.
- Linux limpo fora do ambiente de build, sessão da dashboard no Linux e
  atualização real por `.tar.gz` publicada continuam pendentes. macOS e ARM64
  não foram feitos.
