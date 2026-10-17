(function () {
	var form = document.getElementById("upload-form");
	var input = document.getElementById("upload-file");
	var status = document.getElementById("upload-status");
	var list = document.getElementById("upload-list");

	function setStatus(message) { status.textContent = message; }

	function emptyItem(text) {
		var item = document.createElement("li");
		item.className = "upload-empty";
		item.textContent = text;
		return item;
	}

	function fileItem(name, href) {
		var item = document.createElement("li");
		var link = document.createElement("a");
		var remove = document.createElement("button");
		link.href = href;
		link.textContent = name;
		remove.type = "button";
		remove.className = "copy-button";
		remove.textContent = "Delete";
		remove.addEventListener("click", function () { deleteFile(name, href, remove); });
		item.appendChild(link);
		item.appendChild(remove);
		return item;
	}

	// The server's autoindex page is the source of truth for stored files.
	function refreshList() {
		return fetch("/uploads/", { credentials: "same-origin" }).then(function (response) {
			if (!response.ok)
				throw new Error(response.status + " " + response.statusText);
			return response.text();
		}).then(function (html) {
			var page = new DOMParser().parseFromString(html, "text/html");
			var links = page.querySelectorAll("li a");
			list.textContent = "";
			for (var i = 0; i < links.length; i++) {
				var name = links[i].textContent;
				if (name.charAt(0) === "." || name.charAt(name.length - 1) === "/")
					continue;
				list.appendChild(fileItem(name, links[i].getAttribute("href")));
			}
			if (!list.children.length)
				list.appendChild(emptyItem("No uploaded files yet."));
		}).catch(function (error) {
			list.textContent = "";
			list.appendChild(emptyItem("Could not load /uploads/: " + error.message));
		});
	}

	function deleteFile(name, href, button) {
		button.disabled = true;
		fetch(href, { method: "DELETE", credentials: "same-origin" }).then(function (response) {
			setStatus("DELETE " + href + " -> " + response.status + " " + response.statusText);
			return refreshList();
		}).catch(function (error) {
			button.disabled = false;
			setStatus("DELETE " + name + " failed: " + error.message);
		});
	}

	form.addEventListener("submit", function (event) {
		event.preventDefault();
		if (!input.files.length) {
			setStatus("Choose at least one file first.");
			return;
		}
		setStatus("Uploading " + input.files.length + " file(s)...");
		fetch(form.action, { method: "POST", body: new FormData(form), credentials: "same-origin" }).then(function (response) {
			var location = response.headers.get("Location");
			setStatus("POST /uploads -> " + response.status + " " + response.statusText + (location ? " (" + location + ")" : ""));
			if (response.ok)
				form.reset();
			return refreshList();
		}).catch(function (error) {
			setStatus("Upload failed: " + error.message);
		});
	});

	refreshList();
}());
