// 3D Rotating Cube
// A raymarched 3D cube rendered entirely in a fragment shader.
/// <reference path="./budo.d.ts" />

class ShaderProgram {
    programId

    constructor(programId) {
        this.programId = programId
    }

    use() {
        sys.gl.use(this.programId)
    }

    drawFullscreenImmediate() {
        sys.gl.drawFullscreenImmediate(this.programId);
    }
}

const shaderProgram = new ShaderProgram(sys.gl.createProgram('shader.vert', 'shader.frag'))

function frame() {
    sys.canvas.clear('#0a0a1a')

    sys.canvas.setFillColor('#CCCCCC')
    sys.canvas.drawText('3D Rotating Cube', 20, 32, 22)

    sys.gl.bindScreen()

    shaderProgram.drawFullscreenImmediate()

    sys.animation.requestFrame(frame)
}

sys.animation.requestFrame(frame)
