// Compile-time guardrail: questo file deve compilare senza CHRONON_WITH_VIDEO.
// Se qualcuno inietta FFmpeg nel percorso di core/graph, la define verrà propagata
// e questo target fallirà il build prima ancora di linkare.
#ifdef CHRONON_WITH_VIDEO
#error "GUARDRAIL: CHRONON_WITH_VIDEO e' attivo — FFmpeg e' entrato nel percorso caldo. Controlla le deps di chronon3d_core e chronon3d_graph."
#endif

#include <chronon3d/backends/software/software_renderer.hpp>
#include <chronon3d/render_graph/graph_executor.hpp>
#include <chronon3d/render_graph/graph_builder.hpp>

int main() { return 0; }
