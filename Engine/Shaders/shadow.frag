// shadow.frag — no color output, depth-only (see shadow.vert). PipelineBuilder
// always wants two shader stages, so this is a trivial empty stand-in rather
// than special-casing a one-stage pipeline for just this one variant.
#version 450

void main() {
}
