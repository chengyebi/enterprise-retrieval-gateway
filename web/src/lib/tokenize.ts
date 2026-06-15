const TOKEN_RE = /[a-z0-9]+(?:[_-][a-z0-9]+)*/gi;

export function normalizeText(value: string): string {
  return value.toLowerCase();
}

function expandedTokens(value: string): string[] {
  const raw = normalizeText(value).match(TOKEN_RE) ?? [];
  const tokens: string[] = [];
  for (const token of raw) {
    tokens.push(token);
    if (token.includes('_') || token.includes('-')) {
      for (const part of token.split(/[_-]+/)) {
        if (part.length > 0) {
          tokens.push(part);
        }
      }
    }
  }
  return tokens;
}

export function tokenize(value: string): string[] {
  return Array.from(new Set(expandedTokens(value)));
}

export function tokenCounts(value: string): Map<string, number> {
  const counts = new Map<string, number>();
  for (const token of expandedTokens(value)) {
    counts.set(token, (counts.get(token) ?? 0) + 1);
  }
  return counts;
}

export function containsExactPhrase(haystack: string, needle: string): boolean {
  const normalizedNeedle = normalizeText(needle).trim();
  if (!normalizedNeedle) {
    return false;
  }
  return normalizeText(haystack).includes(normalizedNeedle);
}

export function isCodeLike(token: string): boolean {
  return /^(?:[a-z]*\d+[a-z\d]*|[a-z]+[_-][a-z\d_-]+)$/i.test(token);
}

export function compactNumber(value: number, digits = 3): string {
  return Number.isFinite(value) ? value.toFixed(digits) : '0.000';
}
