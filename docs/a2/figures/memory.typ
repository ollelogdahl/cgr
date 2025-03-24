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

#let popout-elem = (name, fill: white, title: "") => {
  return (
    name: name,
    fill: fill,
    title: title,
  )
}

#let popout = (p, name, o, elems, w: 2, h: 1) => {
  cetz.draw.on-layer(1, {
    cetz.draw.rect(p, (rel: (w, h * elems.len())), fill: white, name: name)

    let index = 0
    for elem in elems {
      let position = (p.at(0), p.at(1) + h * elems.len() - (1 + index) * h)
      cetz.draw.rect(position, (rel: (w, h)), fill: elem.at("fill"), name: elem.at("name"))
      cetz.draw.content(elem.at("name") + ".center", elem.at("title"))

      index = index + 1
    }
  })
  cetz.draw.line(o + ".north-west", name + ".north-west", stroke: (dash: "dashed"))
  cetz.draw.line(o + ".north-east", name + ".north-east", stroke: (dash: "dashed"))
  cetz.draw.line(o + ".south-west", name + ".south-west", stroke: (dash: "dashed"))
  cetz.draw.line(o + ".south-east", name + ".south-east", stroke: (dash: "dashed"))
}

#let collect = (p0, p1, title) => {
  // draw a grippy claw.
  if (p0[0] == p1[0]) {
    // vertical

  } else {
    // horizontal
  }
}

#cetz.canvas(length: 0.6cm, {
  import cetz.draw: *
  cell((0,0), w: 6, "box0", heading: "Vertex buffer")
  cell((7,0), w: 6, "box1", heading: "Index buffer")
  cell((14,0), w: 6, "meshb", heading: "Mesh buffer")
  cell((15,0), w: 1, "mesh", fill: red)
  // cell((0,20), w: 12, "box3", heading: "Draw buffer")
  // cell((0,16), w: 12, "box4", heading: "Object buffer")
  // cell((0,8), w: 12, "box5", heading: "Transform buffer")

  popout((16,-7), w: 4, "test", "mesh", (
    popout-elem("l1", fill: blue, title: "Start Index"),
    popout-elem("l2", fill: blue, title: "End Index"),
    popout-elem("l3", title: "Distance"),
    popout-elem("l4", fill: blue, title: "Start Index"),
    popout-elem("l2", fill: blue, title: "End Index"),
    popout-elem("l3", title: "Distance"),
  ))
})