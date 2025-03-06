#import "@preview/cetz:0.3.1"

#let blue = rgb("#DAE8FC")
#let green = rgb("#D5E8D4")
#let yellow = rgb("#FFF2CC")
#let red = rgb("#F8CECC")
#let purple = rgb("#E1D5E7")

#let cell = (p, name, w: 2, h: 2, fill: white, content: "", heading: "") => {
  cetz.draw.rect(p, (rel: (w, h)), fill: fill, name: name)
  if (content != "") {
    cetz.draw.content(name + ".center", content)
  }
  if (heading != "") {
    cetz.draw.content(name + ".north-west", anchor: "south-west",
      padding: (0.25, 0),
      heading)
  }
}

#let popout = (p, name, o, w: 2, h: 2, elems: ()) => {
  cetz.draw.on-layer(1, {
    cetz.draw.rect(p, (rel: (w, h * length(elems))), fill: white, name: name)
  })
  cetz.draw.line(o + ".north-west", name + ".north-west", stroke: (dash: "dashed"))
  cetz.draw.line(o + ".north-east", name + ".north-east", stroke: (dash: "dashed"))
  cetz.draw.line(o + ".south-west", name + ".south-west", stroke: (dash: "dashed"))
  cetz.draw.line(o + ".south-east", name + ".south-east", stroke: (dash: "dashed"))
  cetz.draw.on-layer(2, {

    for elem in elems {
      cetz.draw.rect(p + elem[0], elem[1], fill: elem[2])
    }
  })
}

#let collect = (p0, p1, title) => {
  // draw a grippy claw.
  if (p0[0] == p1[0]) {
    // vertical

  } else {
    // horizontal
  }
}

#cetz.canvas(length: 0.4cm, {
  import cetz.draw: *
  cell((0,0), w: 10, "box0", heading: "Vertex buffer")
  cell((12,0), w: 10, "box1", heading: "Index buffer")
  cell((24,0), w: 10, "meshb", heading: "Mesh buffer")
  cell((26,0), w: 1, "mesh", fill: red)
  // cell((0,20), w: 12, "box3", heading: "Draw buffer")
  // cell((0,16), w: 12, "box4", heading: "Object buffer")
  // cell((0,8), w: 12, "box5", heading: "Transform buffer")

  popout((25,-4), w: 6, "test", "mesh",)
})