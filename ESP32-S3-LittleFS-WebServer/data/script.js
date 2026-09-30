const statusElement = document.querySelector('#status');

async function loadStatus() {
  statusElement.textContent = 'Loading /api/status…';
  try {
    const response = await fetch('/api/status');
    if (!response.ok) throw new Error(`HTTP ${response.status}`);
    statusElement.textContent = JSON.stringify(await response.json(), null, 2);
  } catch (error) {
    statusElement.textContent = `Could not load status: ${error.message}`;
  }
}

document.querySelector('#refresh').addEventListener('click', loadStatus);
loadStatus();
