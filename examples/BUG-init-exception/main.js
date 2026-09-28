/// <reference path="../budo.d.ts" />

function dumpObj(o, fieldIndent = "  ") {
    const entries = Object.getOwnPropertyNames(o).map(n => [n, o[n]])
        .sort(([keyA, valueA], [keyB, valueB]) => {
            const r = -(typeof valueA).localeCompare(typeof valueB)
            if (r !== 0) return r
            return keyA.localeCompare(keyB)
        })

    if (entries.length === 0) {
        sys.log(`${fieldIndent}empty object`)
        return
    }

    entries.forEach(([key, value]) => {
        let suffix = "[UNKNOWN]"
        switch (typeof value) {
            case "function": suffix = "()"; break
            case "object": suffix = " {"; break
            case "number": suffix = ""; break
        }
        sys.log(`${fieldIndent}${key}${suffix}`)
        if (typeof value === "object") {
            dumpObj(value, fieldIndent + "  ")
            sys.log(`${fieldIndent}}`)
        }
    })
}

dumpObj(sys)

// the executable will report 'JavaScript loaded successfully'
// even with this!!! That's a bug...
sys.toto.titi()
throw "lkjh"
