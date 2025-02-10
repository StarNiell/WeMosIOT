function showAlert() {
  alert("JS works!");
}

function changeLEDState(value) {
  $.post("/led", { ledstate: value });
}
