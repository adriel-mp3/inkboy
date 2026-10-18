# Primeiros passos no InkBoy

Este é um mapa para entender o programa e participar das decisões. Para detalhes
de build e uso, comece pelo [README](../README.md); este guia foca no código e
nas evidências.

## O que roda

InkBoy coordena três partes: SameBoy executa o jogo, o touchscreen fornece os
comandos e FBInk desenha na tela do Kindle. O InkBoy não implementa o emulador
nem acessa o framebuffer diretamente. O runtime é uma sequência single-threaded
coordenada por `src/main.c`.

```text
toque ──> controles ──> SameBoy ──> último quadro disponível ──> FBInk ──> Kindle
             input.c      libretro.c       frame.c / render.c
```

## Siga o ciclo

1. `main.c` abre display, core e touchscreen.
2. O warmup lê o toque e executa até 180 quadros do core.
3. Ao apresentar o primeiro quadro, o renderer salva o framebuffer anterior e
   mostra a composição inicial.
4. Durante o jogo, o loop lê o toque e executa o core continuamente.
5. Na cadência configurada, o renderer mostra o quadro mais recente.
6. Ao sair, o programa fecha toque e core e restaura a tela salva, se chegou a
   salvá-la.

## Encontre o código

| Pergunta | Comece por | Responsabilidade |
| --- | --- | --- |
| Qual é a ordem de execução e saída? | `src/main.c`, `src/timing.c` | Ciclo de vida e prazos do loop |
| Como o toque vira botão ou EXIT? | `src/input.c`, `src/layout.c` | Eventos, áreas e gestos |
| Como o jogo conversa com o emulador? | `src/libretro.c` | Carregamento do core e callbacks |
| Como o quadro vira pixels para a tela? | `src/frame.c`, `src/draw.c`, `src/render.c` | Conversão, composição e FBInk |
| Como é medida uma execução? | `src/profile.c`, `src/profile.h` | Contadores e tempos |
| Como compilar e instalar? | `Makefile`, `mk/`, `bin/deploy` | Build, pacote e deployment |

O `Makefile` da raiz só inclui os arquivos de `mk/`: `docker.mk` organiza os
ambientes, `app.mk` compila e liga o app, `sameboy.mk` compila o core e
`workflow.mk` trata pacotes e deployment.
Os headers `.h` mostram as interfaces entre módulos.

`layout.c` calcula a geometria que `draw.c` usa para desenhar e `input.c` usa
para reconhecer toques. Os percentuais de posição e tamanho ficam nomeados no
topo de `layout.c`; assim, desenho e hit-testing partem das mesmas formas. As
funções `set_*` calculam cada região, e `inkboy_control_at_point()` transforma
um ponto tocado no controle correspondente.

## Decisões que você pode avaliar

| Tema | Código | Evidência e contexto |
| --- | --- | --- |
| Cadência, waveform e área de refresh | `src/presentation.h`, `src/render.c` | [decisão vigente](decisions.md), [escopo e critérios](tasks/TASK-010-reduce-flashing-and-improve-fluidity.md), [resultados medidos](experiments/10-presentation-fluidity.md) |
| Toque e controles | `src/input.c`, `src/layout.c` | [experimento de toque](experiments/03-touch-input.md), [TASK-009](experiments/09-gameplay-runtime.md) |
| Escala e aparência da imagem | `src/frame.c` | [Stage 5.4](experiments/05.4-libretro-continuous-rendering.md) |
| Desempenho e callbacks do core | `src/libretro.c` | [probes Libretro](experiments/05-libretro-probe.md), [Stage 5.3](experiments/05.3-libretro-frame.md) |
| ABI, pacote e instalação | `mk/`, `bin/deploy` | [compatibilidade](compatibility.md), [README](../README.md) |

Para cada mudança, combine: o comportamento que queremos melhorar, a evidência
que sustenta a proposta, o efeito no Kindle real e a menor parte do código que
precisa mudar. `docs/decisions.md` registra escolhas vigentes; `docs/experiments/`
registra o que foi medido; `docs/tasks/` registra escopos de trabalho. Um
resultado de experimento não vira automaticamente padrão do produto.

## Estado atual da tela

O padrão escolhido é 2 FPS, refresh regional GC16 e uma limpeza AUTO com flash a
cada 60 segundos. Essa escolha veio depois do TASK-010, mas os candidatos de
2 FPS não passaram os critérios daquela campanha e a matriz física do padrão
atual segue incompleta. Portanto, “é o padrão” não quer dizer “passou por toda a
aceitação”. O histórico está em [TASK-010](experiments/10-presentation-fluidity.md).

Para avaliar essa decisão, separe três fatos: o usuário preferiu GC16 na
comparação visual; os screenings numéricos não aprovaram nenhum candidato de
2 ou 4 FPS; e eles foram feitos antes de o runtime adiar a espera das
atualizações regionais comuns. Assim, os resultados justificam manter a
aceitação em aberto, mas não medem o comportamento da implementação atual. O
TASK-010 descreve os critérios; o relatório registra os resultados e suas
limitações.

O loop avança o jogo e atualiza a tela em ritmos diferentes: o core pode gerar
vários quadros entre atualizações, e o InkBoy conserva o mais recente. A
atualização regional comum adia a espera de conclusão para continuar lendo o
toque; a espera ocorre antes de um redesenho completo, da limpeza ou da
restauração. Em `main.c`, o prazo de emulação é `next_emulation_run`
(`timespec`); os prazos da tela terminam em `_ns` e usam nanossegundos.

Em `src/render.c`, `inkboy_renderer_present_frame()` segue este caminho:
`prepare_region()` converte e escala; `frame_geometry_changed()` e
`select_presentation_plan()` escolhem apresentação inicial, atualização comum,
limpeza ou imagem repetida; `prepare_presentation()` prepara os buffers,
aguarda trabalho regional pendente por `wait_for_pending_refresh()` quando
necessário, monta a composição completa e salva a tela anterior na primeira
apresentação. Depois,
`draw_frame()` escreve no framebuffer, `submit_refresh()` escolhe waveform e
envia o refresh, e `complete_refresh()` aguarda quando preciso.

## Fatos úteis ao ler mudanças

- O gameplay usa nearest-neighbor em `src/frame.c`. Modos de escala e o probe
  usados em pesquisas antigas foram removidos do código executável.
- O quadro devolvido por `inkboy_libretro_frame()` é uma vista emprestada: o
  adaptador mantém apenas o mais recente, sem fila. Se outro quadro chegar
  antes de o anterior ser selecionado, o anterior é contado como superseded.
  O próximo `run()` pode sobrescrever a vista; por isso o renderer a consome
  antes da próxima execução do core.
- O adaptador mantém um ponteiro global para o core ativo porque callbacks
  Libretro não recebem `userdata`; o runtime suporta um core ativo por vez.
- `make host` verifica compilação local, não compatibilidade ou comportamento
  físico. Para comparar execuções coletadas, `make logs` resume os dados; use o
  mesmo ROM, binário, perfil e condições. A aparência e o movimento precisam
  ser avaliados no Kindle.

## Como o toque vira controle

Na inicialização, `input.c` procura dispositivos `/dev/input/event*` com eixos
de toque válidos e exige exatamente um candidato. O PW5 usa Type B; há um
fallback para single-touch com `BTN_TOUCH`. O descritor selecionado é capturado
durante o jogo e liberado no encerramento.

O protocolo Type B acumula eventos por slot; `SYN_REPORT` fecha o quadro de
eventos e processa a posição do contato principal. Dispositivos single-touch
usam `ABS_X`, `ABS_Y` e `BTN_TOUCH`, mas seguem as mesmas regras de gesto.

| Evento | Efeito no gesto |
| --- | --- |
| `ABS_MT_TRACKING_ID >= 0` | O primeiro contato vira principal; outros slots ficam sem controle até serem soltos. |
| Posição e `SYN_REPORT` | Processa a posição do slot principal. No primeiro quadro, escolhe o controle tocado. |
| Sair da área do controle | Cancela o gesto; voltar para dentro não o reativa. |
| Soltar e `SYN_REPORT` | Um controle emite `UP` se o dedo terminar dentro da mesma área. EXIT encerra se o dedo terminar dentro de sua área sem cancelamento. |
| `SYN_DROPPED` | Cancela o gesto atual e reseta o rastreamento dos slots. |

O centro do D-pad é neutro. Os casos de toque e geometria estão em
`tests/test_input.c` e `tests/test_layout.c`.

## Código de apoio

| Onde | Função |
| --- | --- |
| `tests/` | Contém verificações de layout, toque, render, runtime e profiling. `make test` roda a suíte host; `make libretro-runtime-test` testa o core SameBoy separadamente. |
| `tools/log_report.py` | Resume os logs coletados em `build/logs/` numa tabela compacta. |
| `tools/profile_report.py` | Calcula métricas detalhadas de um log, incluindo p95 dos tempos de espera. |

## Como ler as medidas

As taxas são calculadas dentro da fase indicada no log. `emu_fps` conta chamadas
ao core; `normal_fps` conta apresentações comuns; `display_fps` inclui também a
apresentação inicial e as limpezas. Como a atualização regional comum adia a
espera pelo painel, `normal_fps` representa submissões aceitas, não confirma
que cada pedido virou uma atualização física separada.

Os campos `*_count`, `*_avg_ms`, `*_max_ms` e `*_last_ms` resumem cada etapa.
Alguns tempos se sobrepõem: `callback_copy` ocorre dentro de `emulation`, então
não some as etapas como se fossem partes consecutivas. `input_gap` mede o tempo
entre leituras do toque; `input_gap_clean` separa intervalos atribuídos à
apresentação inicial ou à limpeza da tela. O protocolo e as limitações das
medidas estão em
[TASK-010](experiments/10-presentation-fluidity.md).

## Vocabulário

**Core** é a biblioteca SameBoy; **Libretro** é a interface usada para controlá-la;
**quadro** é uma imagem do jogo; **refresh** é a atualização física da tela
e-ink; **política de apresentação** é a cadência e o tipo de atualização usados
pela tela.
