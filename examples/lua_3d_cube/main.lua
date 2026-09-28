-- Lua 3D Cube — proves sys.gl 3D + sys.math parity in Lua.
-- Builds an interleaved cube (pos+normal) on the fly and rotates it.

local positions = {} -- 24 verts × 3 floats
local normals   = {} -- 24 verts × 3 floats
local indices   = {} -- 36 indices

local faces = {
    {n = { 1,  0,  0}, v = {{ 1,-1,-1},{ 1, 1,-1},{ 1, 1, 1},{ 1,-1, 1}}},
    {n = {-1,  0,  0}, v = {{-1,-1, 1},{-1, 1, 1},{-1, 1,-1},{-1,-1,-1}}},
    {n = { 0,  1,  0}, v = {{-1, 1,-1},{-1, 1, 1},{ 1, 1, 1},{ 1, 1,-1}}},
    {n = { 0, -1,  0}, v = {{-1,-1, 1},{-1,-1,-1},{ 1,-1,-1},{ 1,-1, 1}}},
    {n = { 0,  0,  1}, v = {{-1,-1, 1},{ 1,-1, 1},{ 1, 1, 1},{-1, 1, 1}}},
    {n = { 0,  0, -1}, v = {{ 1,-1,-1},{-1,-1,-1},{-1, 1,-1},{ 1, 1,-1}}},
}

local pi, ni, ii = 1, 1, 1
for f = 1, 6 do
    local face = faces[f]
    local base = (f - 1) * 4
    for k = 1, 4 do
        positions[pi]   = face.v[k][1]
        positions[pi+1] = face.v[k][2]
        positions[pi+2] = face.v[k][3]
        normals[ni]     = face.n[1]
        normals[ni+1]   = face.n[2]
        normals[ni+2]   = face.n[3]
        pi = pi + 3
        ni = ni + 3
    end
    indices[ii]   = base + 0
    indices[ii+1] = base + 1
    indices[ii+2] = base + 2
    indices[ii+3] = base + 0
    indices[ii+4] = base + 2
    indices[ii+5] = base + 3
    ii = ii + 6
end

local program = sys.gl.createProgram("cube.vert", "cube.frag")
local pos_buf = sys.gl.createBuffer("vertex", positions, "f32")
local nrm_buf = sys.gl.createBuffer("vertex", normals,   "f32")
local idx_buf = sys.gl.createBuffer("index",  indices,   "u16")

local layout = sys.gl.createVertexLayout()
sys.gl.setAttribute(layout, 0 --[[a_position]], pos_buf, 3, "float", false, 0, 0)
sys.gl.setAttribute(layout, 2 --[[a_normal  ]], nrm_buf, 3, "float", false, 0, 0)
sys.gl.setIndexBuffer(layout, idx_buf, "u16")

local proj  = {0,0,0,0, 0,0,0,0, 0,0,0,0, 0,0,0,0}
local view  = {0,0,0,0, 0,0,0,0, 0,0,0,0, 0,0,0,0}
local model = {0,0,0,0, 0,0,0,0, 0,0,0,0, 0,0,0,0}
local tmp   = {0,0,0,0, 0,0,0,0, 0,0,0,0, 0,0,0,0}
local mvp   = {0,0,0,0, 0,0,0,0, 0,0,0,0, 0,0,0,0}

local eye    = {0, 0, 5}
local target = {0, 0, 0}
local up     = {0, 1, 0}

local function frame(timestamp)
    local w = sys.window.getWidth()
    local h = sys.window.getHeight()
    local t = timestamp / 1000.0

    sys.canvas.clear("#101020")
    sys.canvas.setFillColor("#FFFFFF")
    sys.canvas.drawText("Lua 3D Cube", 20, 32, 22)
    sys.canvas.setFillColor("#888888")
    sys.canvas.drawText("Lua + sys.gl 3D + sys.math parity", 20, 56, 14)

    sys.math.mat4Perspective(proj, math.pi / 3, w / h, 0.1, 100.0)
    sys.math.mat4LookAt(view, eye, target, up)

    sys.math.mat4Identity(model)
    sys.math.mat4RotateY(model, model, t * 0.7)
    sys.math.mat4RotateX(model, model, t * 0.4)

    sys.math.mat4Multiply(tmp, view, model)
    sys.math.mat4Multiply(mvp, proj, tmp)

    sys.gl.setUniformMatrix4(program, "u_mvp", mvp)
    sys.gl.setUniformMatrix4(program, "u_model", model)

    sys.gl.drawMesh(program, layout, {
        mode = "triangles",
        first = 0,
        count = 36,
        depthTest = true,
        depthWrite = true,
        cull = "back",
        blend = "none",
    })

    sys.animation.requestFrame(frame)
end

sys.animation.requestFrame(frame)
