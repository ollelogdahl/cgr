#import "@preview/cetz:0.3.2"
#set page(width: 50em, height: 40em)

#cetz.canvas({
  import cetz.draw: *
  
  let node-width = 3
  let node-height = 1
  let horiz-sep = 1.2
  let vert-sep = 4
  let arrow-style = (end: "stealth", fill: black, scale: .5)
  let (gray, blue, green, orange, yellow, red, purple) = (rgb("#F5F5F5"), rgb("#DAE8FC"), rgb("#D5E8D4"), rgb("#FFE6CC"), rgb("#FFF2CC"), rgb("#F8CECC"), rgb("#E1D5E7"))

  let resource-start-pos = state("p", (0, 1))

  let box(pos, label, fill: none, name: none, input: false) = {
    rect(
      pos,
      (rel: (node-width, node-height)),
      fill: fill,
      stroke: black,
      name: name,
    )
    content(name, label, inset: 1em)
  }

  let resource(p, label, name: none) = {
    line(p, (rel: (10, 0)), name: name)
    content(
      ("line.start"), anchor: "south", label)
  }

  box((0, 0), [New Objects], fill: blue, name: "host")
  resource((0, -2), "Test", name: "r1")
  resource((0, -3), "Test", name: "r2")

})