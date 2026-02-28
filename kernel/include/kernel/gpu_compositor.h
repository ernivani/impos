#ifndef _KERNEL_GPU_COMPOSITOR_H
#define _KERNEL_GPU_COMPOSITOR_H

/*
 * GPU-accelerated compositor using raw virgl (Gallium3D) commands.
 *
 * Composites window surfaces as textured quads with alpha blending,
 * rendered by the host GPU via VirtIO GPU 3D.  Falls back to the
 * software compositor when virgl is unavailable.
 */

/* Initialize the GPU compositor.
   Returns 1 on success (GPU path active), 0 on failure (use SW fallback). */
int  gpu_comp_init(void);

/* Shut down the GPU compositor, releasing all virgl resources. */
void gpu_comp_shutdown(void);

/* Returns 1 if the GPU compositor is currently active. */
int  gpu_comp_is_active(void);

/* Render a full frame: upload dirty textures, draw quads, readback, flip. */
void gpu_comp_render_frame(void);

/* Notify that a compositor surface was created at pool index pool_idx. */
void gpu_comp_surface_created(int pool_idx, int w, int h);

/* Notify that a compositor surface was destroyed. */
void gpu_comp_surface_destroyed(int pool_idx);

/* Notify that a compositor surface was resized. */
void gpu_comp_surface_resized(int pool_idx, int new_w, int new_h);

#endif /* _KERNEL_GPU_COMPOSITOR_H */
