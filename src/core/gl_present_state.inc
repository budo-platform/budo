#define BUDO_GL_PREPARE_SCREEN_PRESENT(width, height) \
    do                                                    \
    {                                                     \
        glBindFramebuffer(GL_FRAMEBUFFER, 0);             \
        glViewport(0, 0, (width), (height));              \
        glDisable(GL_SCISSOR_TEST);                       \
        glDisable(GL_DEPTH_TEST);                         \
        glDisable(GL_CULL_FACE);                          \
        glColorMask(GL_TRUE, GL_TRUE, GL_TRUE, GL_TRUE);  \
        glDepthMask(GL_FALSE);                            \
    } while (0)

#define BUDO_GL_APPLY_TEXTURE_2D_LINEAR_CLAMP()                         \
    do                                                                      \
    {                                                                       \
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);   \
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);   \
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE); \
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE); \
    } while (0)
