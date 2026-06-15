import type { DemoData, DemoDocument } from '../types';

export async function loadDemoData(): Promise<DemoData> {
  const base = import.meta.env.BASE_URL || '/';
  const candidates = [
    `${base}data/demo_data.json`,
    '/data/demo_data.json',
    'data/demo_data.json',
  ];

  let lastError: unknown = null;
  for (const candidate of candidates) {
    try {
      const response = await fetch(candidate, { cache: 'no-cache' });
      if (!response.ok) {
        throw new Error(`${response.status} ${response.statusText}`);
      }
      return (await response.json()) as DemoData;
    } catch (error) {
      lastError = error;
    }
  }
  throw new Error(`Unable to load static demo data: ${String(lastError)}`);
}

export function documentIndexByChunk(documents: DemoDocument[]): Map<string, DemoDocument> {
  return new Map(documents.map((document) => [document.chunk_id, document]));
}
