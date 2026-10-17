// Evaluation guide: runs each check as a same-origin request and compares the
// status code with the one the evaluation expects.
(function () {
	"use strict";

	var checks = document.querySelectorAll(".check[data-method], .check[data-action]");
	var progress = document.getElementById("progress");
	var passed = {};

	function updateProgress() {
		var count = 0;
		for (var key in passed)
			if (passed[key])
				count++;
		progress.textContent = count + " / " + checks.length + " passed";
	}

	function resultBox(check) {
		var box = check.querySelector(".result");
		if (!box) {
			box = document.createElement("pre");
			box.className = "result";
			check.appendChild(box);
		}
		return box;
	}

	function setState(check, state, label) {
		var chip = check.querySelector(".expect");
		chip.classList.remove("is-pass", "is-fail", "is-running");
		if (state)
			chip.classList.add("is-" + state);
		if (label)
			chip.textContent = label;
	}

	// Sends one request and resolves with its status, headers, and body text.
	// Redirects are not followed, so a 3xx stays visible as a redirect.
	function send(method, path, body) {
		var init = { method: method, credentials: "same-origin", redirect: "manual", cache: "no-store" };
		if (body !== null && body !== undefined && body !== "") {
			init.body = body;
			init.headers = { "Content-Type": "text/plain" };
		}
		var started = Date.now();
		return fetch(path, init).then(function (response) {
			return response.text().then(function (text) {
				return { method: method, path: path, response: response, text: text, ms: Date.now() - started };
			});
		});
	}

	function statusOf(result) {
		return result.response.type === "opaqueredirect" ? "redirect" : String(result.response.status);
	}

	function describe(result) {
		var response = result.response;
		var lines = [result.method + " " + result.path];
		if (response.type === "opaqueredirect") {
			lines.push("-> redirect (" + result.ms + " ms). The browser hides 3xx details from scripts;");
			lines.push("   open the link and check the Network tab for the status and Location header.");
			return lines.join("\n");
		}
		lines.push("-> " + response.status + " " + response.statusText + " (" + result.ms + " ms)");
		response.headers.forEach(function (value, name) {
			lines.push(name + ": " + value);
		});
		if (result.text) {
			var preview = result.text.length > 900 ? result.text.slice(0, 900) + "\n..." : result.text;
			lines.push("", preview);
		}
		return lines.join("\n");
	}

	// POST a file into /uploads, DELETE it, then confirm the GET is a 404.
	function deleteCycle() {
		var path = "/uploads/eval-delete-" + Date.now() + ".txt";
		var results = [];
		return send("POST", path, "created by the evaluation guide").then(function (created) {
			results.push(created);
			return send("DELETE", path);
		}).then(function (deleted) {
			results.push(deleted);
			return send("GET", path);
		}).then(function (gone) {
			results.push(gone);
			return results;
		});
	}

	function run(check) {
		var expected = check.getAttribute("data-expect").split(" ");
		var box = resultBox(check);
		var started = Date.now();
		var timer = setInterval(function () {
			setState(check, "running", Math.round((Date.now() - started) / 1000) + " s");
		}, 1000);
		setState(check, "running", "...");
		box.textContent = "Waiting for the server...";

		var pending = check.getAttribute("data-action") === "delete-cycle" ? deleteCycle() :
			send(check.getAttribute("data-method"), check.getAttribute("data-path"), check.getAttribute("data-body"));

		return pending.then(function (outcome) {
			var results = outcome instanceof Array ? outcome : [outcome];
			var statuses = [];
			for (var i = 0; i < results.length; i++)
				statuses.push(statusOf(results[i]));
			var ok = statuses.join(" ") === expected.join(" ");
			box.textContent = results.map(describe).join("\n\n");
			setState(check, ok ? "pass" : "fail", statuses.join(" → ").replace("redirect", "3xx"));
			passed[check.id] = ok;
		}).catch(function (error) {
			box.textContent = "Request failed: " + error.message;
			setState(check, "fail", "error");
			passed[check.id] = false;
		}).then(function () {
			clearInterval(timer);
			updateProgress();
		});
	}

	for (var i = 0; i < checks.length; i++) {
		(function (check, index) {
			check.id = check.id || "check-" + index;
			check.querySelector(".run").addEventListener("click", function () { run(check); });
		}(checks[i], i));
	}

	// "Run section" runs that section's checks one after another, so a slow
	// CGI check does not hide the result of the next one.
	var sectionButtons = document.querySelectorAll(".run-all");
	for (var s = 0; s < sectionButtons.length; s++) {
		sectionButtons[s].addEventListener("click", function (event) {
			var button = event.currentTarget;
			var section = button.closest("section");
			var list = section.querySelectorAll(".check[data-method], .check[data-action]");
			var chain = Promise.resolve();
			button.disabled = true;
			for (var j = 0; j < list.length; j++)
				chain = chain.then(run.bind(null, list[j]));
			chain.then(function () { button.disabled = false; });
		});
	}

	// Body-limit form: preset a short or long body before posting.
	var fills = document.querySelectorAll(".fill");
	for (var f = 0; f < fills.length; f++) {
		fills[f].addEventListener("click", function (event) {
			var form = event.currentTarget.closest("form");
			form.querySelector("textarea").value = event.currentTarget.getAttribute("data-fill");
		});
	}

	// Request lab: any method, any path, optional body.
	var lab = document.getElementById("lab-form");
	var labOutput = document.getElementById("lab-output");
	lab.addEventListener("submit", function (event) {
		event.preventDefault();
		var method = document.getElementById("lab-method").value;
		var path = document.getElementById("lab-path").value || "/";
		if (path.charAt(0) !== "/")
			path = "/" + path;
		labOutput.textContent = "Waiting for the server...";
		send(method, path, document.getElementById("lab-body").value).then(function (result) {
			labOutput.textContent = describe(result);
		}).catch(function (error) {
			labOutput.textContent = "Request failed: " + error.message;
		});
	});

	updateProgress();
}());
