(function () {
	var endpoint = document.getElementById("endpoint");
	var runButton = document.getElementById("run-request");
	var openLink = document.getElementById("open-endpoint");
	var status = document.getElementById("inspector-status");
	var output = document.getElementById("inspector-output");
	function selectedEndpoint() { return endpoint.options[endpoint.selectedIndex].value; }
	function updateOpenLink() { openLink.href = selectedEndpoint(); }
	function formatHeaders(headers) {
		var lines = [];
		headers.forEach(function (value, name) { lines.push(name + ": " + value); });
		return lines.length ? lines.join("\n") : "(no response headers exposed)";
	}
	function inspectResponse() {
		var path = selectedEndpoint();
		runButton.disabled = true;
		runButton.textContent = "Inspecting...";
		status.textContent = "Requesting " + path + " from this server...";
		output.textContent = "Waiting for response...";
		fetch(path, { credentials: "same-origin" }).then(function (response) {
			return response.text().then(function (body) {
				var shown = body.length > 6000 ? body.slice(0, 6000) + "\n\n[body truncated]" : body;
				status.textContent = response.status + " " + response.statusText + " from " + path;
				output.textContent = "STATUS\n" + response.status + " " + response.statusText + "\n\nHEADERS\n" + formatHeaders(response.headers) + "\n\nBODY\n" + shown;
			});
		}).catch(function (error) {
			status.textContent = "Request failed.";
			output.textContent = "ERROR\n" + error.message;
		}).then(function () {
			runButton.disabled = false;
			runButton.textContent = "Inspect response";
		});
	}
	function copyCommand(button) {
		var command = button.getAttribute("data-copy");
		if (!navigator.clipboard) { button.textContent = "Select manually"; return; }
		navigator.clipboard.writeText(command).then(function () { button.textContent = "Copied"; setTimeout(function () { button.textContent = "Copy"; }, 1300); });
	}
	endpoint.addEventListener("change", updateOpenLink);
	runButton.addEventListener("click", inspectResponse);
	var copyButtons = document.querySelectorAll("[data-copy]");
	for (var i = 0; i < copyButtons.length; ++i) copyButtons[i].addEventListener("click", function () { copyCommand(this); });
}());
