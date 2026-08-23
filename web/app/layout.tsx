import type { Metadata } from "next";
import "./globals.css";

export const metadata: Metadata = {
  title: "AntChain Workbench",
  description: "Run and inspect AntChain's C++ proof-of-work experiments."
};

export default function RootLayout({ children }: Readonly<{ children: React.ReactNode }>) {
  // Browser extensions can add attributes to <html> before React hydrates.
  // Keep that external document-level mutation from producing a false-positive
  // hydration warning without suppressing warnings within the application.
  return <html lang="en" suppressHydrationWarning><body>{children}</body></html>;
}
