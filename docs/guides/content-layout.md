# Conteúdo local, discos e DLCs

Conteúdo original fica em `.local/inputs/`, ignorado pelo Git. O diretório
contém somente `dashboards/`, `games/` e instruções existentes. Capturas, shaders
diagnósticos e caches pertencem a `.local/evidence/` ou a saídas de build.

```text
.local/inputs/
├── dashboards/
│   ├── <build>/
│   │   ├── <perfil>/             conjunto original dessa versão
│   │   └── <procedência>/        pacotes, extrações e metadados preservados
│   ├── metadata/                procedência da coleção histórica
│   └── resources/               acervo não associado a um perfil: nunca fallback
└── games/
    └── <jogo ou Title ID>/
        ├── content/             conteúdo de pacote/demo, quando existente
        ├── disc-1/              arquivos extraídos do primeiro disco
        ├── disc-2/              somente se esse disco tiver sido extraído
        ├── dlc/<pacote>/         conteúdo adicional separado do executável base
        ├── source/              originais preservados, quando existentes
        └── metadata/            hashes, identificação e procedência existentes
```

Não criar antecipadamente discos, DLCs ou níveis vazios. Os dois arquivos 7z de
Forza Motorsport 4 foram agrupados em `games/forza_motorsport_4/`, sem extração
nem criação de discos vazios. Os DLCs existentes de Motocross continuam separados.
Essa organização não implementa troca de disco, instalação de DLC ou licenças:
preserva os contratos existentes e prevê espaço para esses conteúdos.

Importações automáticas usam `games/<Title ID>/<fingerprint>` para pacotes e
`games/<Title ID>/disc-<n>-<fingerprint>` para discos identificados. O fingerprint
completo identifica todos os arquivos e separa revisões/regiões/discos que não
podem compartilhar um diretório gravável. Não deriva compatibilidade do nome
ou do Title ID. Um catálogo já existente continua sendo lido pelo `content_root`
registrado; reimportação reutiliza o destino anterior, inclusive após interrupção.

## Dashboard selecionada pelo perfil

O caminho vem de `[project].game_root` do manifesto; a versão, os hashes e a
procedência vêm do `preparation.toml` correspondente. O guard de CMake também
lê esses arquivos, em vez de construir um caminho global para uma build fixa.
Cada título continua tendo seus módulos, análise AOT e recursos específicos.
O supervisor recebe o manifesto escolhido; não procura módulos pelo basename.

Na coleção migrada, o conjunto ativo da 17559 está em
`dashboards/17559/runtime/`; o perfil 6717 aponta para
`dashboards/6717/blades_retail/`. Esses são caminhos declarados pelos perfis,
não defaults embutidos no supervisor ou no desktop. Não misturar as duas versões
nem aproveitar fontes/teclados de versões diferentes. Ausência ou hash divergente
continua produzindo erro antes do carregamento.

As opções diagnósticas da intro também pertencem ao manifesto:
`[x360n].skip_boot_intro_args` declara overrides `--opção=valor` usados por
`x360n session --skip-boot-intro`. O supervisor aplica as mesmas reservas de
argumentos de sessão; não conhece o nome do cvar de uma dashboard específica.

## Migração e preparação

O inventário, o mapa de caminhos, os backups dos registros de catálogo e os
hashes antes/depois desta migração ficam em
`.local/evidence/reorganization/2026-10-17/inputs-migration/`. Os dados originais foram renomeados
no mesmo volume. Metadados históricos continuam registrando o caminho da coleta;
o mapa liga esses caminhos aos destinos atuais. Os manifestos AOT locais e o
`content_root` do catálogo ativo foram atualizados.

Uma mudança de fonte/manifesto invalida o recibo de preparação anterior.
Preparar pelo fluxo normal gera/builda e valida a nova identidade. Não editar
hashes ou recibos para esconder essa invalidação. Nenhum save/perfil é movido
como parte da migração de inputs.

O campo histórico `reference_examples.demo-iruka.extracted_to` do lock conserva
o endereço da aquisição original. Ele não é consumido pelo build; o manifesto
do título e o mapa da migração registram o destino atual. O lock e os hashes
fixados foram preservados integralmente.
