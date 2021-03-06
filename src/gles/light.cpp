#include "zgl.hpp"
#include "zmath.hpp"
#include "specbuf.hpp"
#include "light.hpp"

namespace fp {

void glMaterialx(GLenum face, GLenum pname, tGLfixed param) {
    tGLfixed v[4] = {param, 0, 0, 0};
    glMaterialxv(face, pname, v);
}

void glMaterialxv(GLenum face, GLenum pname, const tGLfixed *v) {
    GLContext *c = gl_get_context();
    int i;
    GLMaterial *m;

    if (face == GL_FRONT_AND_BACK) {
        glMaterialxv(GL_FRONT, pname, v);
        face = GL_BACK;
    }
    if (face == GL_FRONT) m = &c->material.materials[0];
    else m = &c->material.materials[1];

    switch(pname) {
        case GL_EMISSION:
            for (i = 0; i < 4; i++)
                m->emission.v[i] = v[i];
            break;
        case GL_AMBIENT:
            for (i = 0; i < 4; i++)
                m->ambient.v[i] = v[i];
            break;
        case GL_DIFFUSE:
            for (i = 0; i < 4; i++)
                m->diffuse.v[i] = v[i];
            break;
        case GL_SPECULAR:
            for (i = 0; i < 4; i++)
                m->specular.v[i] = v[i];
            break;
        case GL_SHININESS:
            m->shininess = v[0];
            m->shininess_i = (v[0] / 128.0f) * SPECULAR_BUFFER_RESOLUTION;
            break;
        case GL_AMBIENT_AND_DIFFUSE:
            for (i = 0; i < 4; i++)
                m->diffuse.v[i] = v[i];
            for (i = 0; i < 4; i++)
                m->ambient.v[i] = v[i];
            break;
        default:
            assert(0);
    }
}

void glColorMaterial(GLenum face, GLenum mode) {
    GLContext *c = gl_get_context();
    c->material.color.current_mode = face;
    c->material.color.current_type = mode;
}

void glLightx(GLenum light, GLenum pname, tGLfixed param) {
    tGLfixed v[4] = {pname, 0, 0, 0};
    glLightxv(light, pname, v);
}

void glLightxv(GLenum light, GLenum pname, const tGLfixed *param) {
    GLContext *c = gl_get_context();
    // TODO: 3 components?
    GLLight *l;
    int i;

    assert(light >= GL_LIGHT0 && light < GL_LIGHT0 + MAX_LIGHTS );

    V4 v = *(V4 *)param;
    l = &c->light.lights[light - GL_LIGHT0];

    switch(pname) {
        case GL_AMBIENT:
            l->ambient = v;
            break;
        case GL_DIFFUSE:
            l->diffuse = v;
            break;
        case GL_SPECULAR:
            l->specular = v;
            break;
        case GL_POSITION:
            {
                V4 pos;
                gl_M4_MulV4(&pos, c->matrix.stack_ptr[0], &v);

                l->position = pos;

                if (l->position.v[3] == 0) {
                    l->norm_position.X = pos.X;
                    l->norm_position.Y = pos.Y;
                    l->norm_position.Z = pos.Z;

                    gl_V3_Norm(&l->norm_position);
                }
            }
            break;
        case GL_SPOT_DIRECTION:
            for (i = 0; i < 3; i++) {
                l->spot_direction.v[i] = v.v[i];
                l->norm_spot_direction.v[i] = v.v[i];
            }
            fp::gl_V3_Norm(&l->norm_spot_direction);
            break;
        case GL_SPOT_EXPONENT:
            l->spot_exponent = v.v[0];
            break;
        case GL_SPOT_CUTOFF:
            {
                tGLfixed a = v.v[0];
                assert(a == 180 || (a >= 0 && a <= 90));
                l->spot_cutoff = a;
                if (a != 180) l->cos_spot_cutoff = cos(a * GLfixed_PI / 180.0);
            }
            break;
        case GL_CONSTANT_ATTENUATION:
            l->attenuation[0] = v.v[0];
            break;
        case GL_LINEAR_ATTENUATION:
            l->attenuation[1] = v.v[0];
            break;
        case GL_QUADRATIC_ATTENUATION:
            l->attenuation[2] = v.v[0];
            break;
        default:
            assert(0);
    }
}

void glLightModeli(GLenum pname, GLint param) {
    tGLfixed v[4] = {pname, 0, 0, 0};
    glLightModelxv(pname, v);
}

void glLightModelxv(GLenum pname, const tGLfixed *param) {
    GLContext *c = gl_get_context();
    V4 v = *(V4 *)param;

    switch(pname) {
        case GL_LIGHT_MODEL_AMBIENT:
            c->light.model.ambient = v;
            break;
            // todo(alpi): enum is not defined in gles/gl.h
//        case GL_LIGHT_MODEL_LOCAL_VIEWER:
//            c->light.model.local = (int)v.v[0];
//            break;
        case GL_LIGHT_MODEL_TWO_SIDE:
            c->light.model.two_side = (int)v.v[0];
            break;
        default:
            fprintf(stderr, "glLightModel: illegal pname: 0x%x\n", pname);
            break;
    }
}


static inline tGLfixed clampx(tGLfixed a, tGLfixed min, tGLfixed max) {
    if (a < min) return min;
    else if (a > max) return max;
    else return a;
}

/* non optimized lighting model */
void gl_shade_vertex(GLContext *c, GLVertex *v) {
    tGLfixed R, G, B, A;
    GLMaterial *m;
    GLLight *l;
    V3 n, s, d;
    tGLfixed dist, tmp, att;
    int twoside = c->light.model.two_side;

    m = &c->material.materials[0];

    n.X = v->normal.X;
    n.Y = v->normal.Y;
    n.Z = v->normal.Z;

    R = m->emission.v[0] + m->ambient.v[0] * c->light.model.ambient.v[0];
    G = m->emission.v[1] + m->ambient.v[1] * c->light.model.ambient.v[1];
    B = m->emission.v[2] + m->ambient.v[2] * c->light.model.ambient.v[2];
    A = clampx(m->diffuse.v[3], 0, 1);

    for (l = c->light.first; l != NULL; l = l->next) {
        tGLfixed lR, lB, lG;

        /* ambient */
        lR = l->ambient.v[0] * m->ambient.v[0];
        lG = l->ambient.v[1] * m->ambient.v[1];
        lB = l->ambient.v[2] * m->ambient.v[2];

        if (l->position.v[3] == 0) {
            /* light at infinity */
            d.X = l->position.v[0];
            d.Y = l->position.v[1];
            d.Z = l->position.v[2];
            att = 1;
        } else {
            /* distance attenuation */
            d.X = l->position.v[0] - v->ec.v[0];
            d.Y = l->position.v[1] - v->ec.v[1];
            d.Z = l->position.v[2] - v->ec.v[2];
            dist = sqrt(d.X * d.X + d.Y * d.Y + d.Z * d.Z);
            if (dist > 1E-3) {
                tmp = 1 / dist;
                d.X = fp::multiply(d.X, tmp);
                d.Y = fp::multiply(d.Y, tmp);
                d.Z = fp::multiply(d.Z, tmp);
            }
            att = 1.0f / (l->attenuation[0] + dist * (l->attenuation[1] + dist * l->attenuation[2]));
        }
        tGLfixed dot = d.X * n.X + d.Y * n.Y + d.Z * n.Z;
        if (twoside && dot < 0) dot = -dot;
        if (dot > 0) {
            /* diffuse light */
            lR += dot * l->diffuse.v[0] * m->diffuse.v[0];
            lG += dot * l->diffuse.v[1] * m->diffuse.v[1];
            lB += dot * l->diffuse.v[2] * m->diffuse.v[2];

            /* spot light */
            if (l->spot_cutoff != 180) {
                tGLfixed dot_spot = -(d.X * l->norm_spot_direction.v[0] + d.Y * l->norm_spot_direction.v[1] + d.Z * l->norm_spot_direction.v[2]);
                if (twoside && dot_spot < 0) dot_spot = -dot_spot;
                if (dot_spot < l->cos_spot_cutoff) {
                    /* no contribution */
                    continue;
                } else {
                    /* TODO: optimize */
                    if (l->spot_exponent > 0) {
                        att = att * ::pow(dot_spot, l->spot_exponent);
                    }
                }
            }

            /* specular light */

            if (c->light.model.local) {
                V3 vcoord;
                vcoord.X = v->ec.X;
                vcoord.Y = v->ec.Y;
                vcoord.Z = v->ec.Z;
                gl_V3_Norm(&vcoord);
                s.X = d.X - vcoord.X;
                s.Y = d.Y - vcoord.X;
                s.Z = d.Z - vcoord.X;
            } else {
                s.X = d.X;
                s.Y = d.Y;
                s.Z = d.Z + 1.0;
            }
            tGLfixed dot_spec = n.X * s.X + n.Y * s.Y + n.Z * s.Z;
            if (twoside && dot_spec < 0) dot_spec = -dot_spec;
            if (dot_spec > 0) {
                GLSpecBuf *specbuf;
                int idx;
                tmp = sqrt(s.X * s.X + s.Y * s.Y + s.Z * s.Z);
                if (tmp > 1E-3) {
                    dot_spec = dot_spec / tmp;
                }

                /* TODO: optimize */
                /* testing specular buffer code */
                /* dot_spec= pow(dot_spec, m->shininess);*/
                specbuf = specbuf_get_buffer(c, m->shininess_i, m->shininess);
                idx = (int)(dot_spec * SPECULAR_BUFFER_SIZE);
                if (idx > SPECULAR_BUFFER_SIZE) idx = SPECULAR_BUFFER_SIZE;
                dot_spec = specbuf->buf[idx];
                lR += dot_spec * l->specular.v[0] * m->specular.v[0];
                lG += dot_spec * l->specular.v[1] * m->specular.v[1];
                lB += dot_spec * l->specular.v[2] * m->specular.v[2];
            }
        }

        R += att * lR;
        G += att * lG;
        B += att * lB;
    }

    v->color.v[0] = clampx(R, 0, 1);
    v->color.v[1] = clampx(G, 0, 1);
    v->color.v[2] = clampx(B, 0, 1);
    v->color.v[3] = A;
}

} // namespace fp
